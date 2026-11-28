#include <genealogy/service/person_service.hpp>

#include <genealogy/service/attachment_service.hpp>

#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/migration_repository.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/ulid.hpp>

namespace genealogy {

PersonService::PersonService(Database& db)
    : db_(&db), repo_(db) {}

namespace {

// 空串可选 ID 折叠为未设置（CLI 传 --father "" / JSON father_id:"" 等场景）。
// 必须在落库前对副本执行——校验函数里的折叠只作用于它自己的局部副本，
// 若不用规范化后的对象落库，空串会作为外键写入触发 FOREIGN KEY 失败。
void normalize_ids(Person& p) {
  if (p.father_id && p.father_id->empty()) p.father_id.reset();
  if (p.mother_id && p.mother_id->empty()) p.mother_id.reset();
  if (p.branch_id && p.branch_id->empty()) p.branch_id.reset();
  if (p.portrait_attachment_id && p.portrait_attachment_id->empty())
    p.portrait_attachment_id.reset();
  // 地点外键同样不接受空串（sqlite_util::bind_opt_text 会绑定 ''）
  if (p.birth_place_id && p.birth_place_id->empty()) p.birth_place_id.reset();
  if (p.death_place_id && p.death_place_id->empty()) p.death_place_id.reset();
  if (p.burial_place_id && p.burial_place_id->empty()) p.burial_place_id.reset();
}

// 拒绝制造祖先环：draft 的任一父母链向上追溯不得回到 draft 本人
// （直指父亲=自己，或多代绕回都算）。与 branch_service 的环检测同纪律：
// 世系世代算法对环节点直接失效，环数据一旦落库只能靠 R11 事后报警。
// kMaxHops 只是对库中已有环数据的兜底，正常谱系远达不到。
Error check_ancestor_cycle(const Person& draft, PersonRepository& repo) {
  if (draft.id.empty()) return {};  // 新建档尚无 id，不可能成环
  std::vector<std::string> frontier;
  if (draft.father_id) frontier.push_back(*draft.father_id);
  if (draft.mother_id) frontier.push_back(*draft.mother_id);
  std::set<std::string> visited;  // 已展开的祖先节点
  constexpr int kMaxHops = 1000;
  for (int hops = 0; !frontier.empty() && hops < kMaxHops; ++hops) {
    const std::string cur = frontier.back();
    frontier.pop_back();
    if (cur == draft.id) {
      return invalid_argument("父母关系形成祖先环（" + cur +
                              " 不能既是本人又是本人的祖先）");
    }
    if (!visited.insert(cur).second) continue;
    auto p = repo.get(cur);
    if (!p) continue;  // 不存在由存在性校验报错，这里只防环
    for (const auto* next : {&p->father_id, &p->mother_id}) {
      if (*next && visited.insert(**next).second) frontier.push_back(**next);
    }
  }
  return {};
}

// 建档与更新共用的领域校验。空 Error 表示通过。
Error validate_person(const Person& draft, PersonRepository& repo) {
  if (draft.family_name.empty()) return invalid_argument("姓必填");
  if (draft.given_name.empty()) return invalid_argument("名必填");

  // 空串可选 ID 由 normalize_ids() 在落库前折叠；此处只校验。
  // 父母存在性（拟制关系在 M1 与 parent_child 对齐后扩展）
  if (draft.father_id && !draft.father_id->empty()) {
    if (!is_valid_ulid(*draft.father_id)) {
      return invalid_argument("father_id 不是合法 ULID: " + *draft.father_id);
    }
    auto father = repo.get(*draft.father_id);
    if (!father) return not_found("父亲不存在: " + *draft.father_id);
  }
  if (draft.mother_id && !draft.mother_id->empty()) {
    if (!is_valid_ulid(*draft.mother_id)) {
      return invalid_argument("mother_id 不是合法 ULID: " + *draft.mother_id);
    }
    auto mother = repo.get(*draft.mother_id);
    if (!mother) return not_found("母亲不存在: " + *draft.mother_id);
  }
  return check_ancestor_cycle(draft, repo);
}

// M9.1 压测与第四轮实测：多连接并发写时，WAL 写锁持有者的提交/检查点
// 偶发超过 busy_timeout，裸 autocommit INSERT 直接以 "database is locked"
// 失败且无应用层重试（test_writer_serialization 偶发 249/250）。对锁定类
// 失败做有限次重试：SQLite 侧每次已等满 5s，重试间隔取短值即可。
template <typename F>
auto retry_on_locked(F&& f) -> decltype(f()) {
  for (int attempt = 0;; ++attempt) {
    auto r = f();
    if (r || attempt >= 4 ||
        r.error().message().find("locked") == std::string::npos) {
      return r;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

}  // namespace

Result<Person> PersonService::create_person(const Person& draft) {
  Person normalized = draft;
  normalize_ids(normalized);
  if (auto err = validate_person(normalized, repo_); err) return err;
  return retry_on_locked([this, &normalized] {
    // id 在 insert 内生成：重试时重新取号，失败的尝试什么都没写。
    return repo_.insert(normalized);
  });
}

Result<Person> PersonService::get_person(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Person>> PersonService::list_persons(
    const PersonFilter& filter) {
  return repo_.list(filter);
}

Result<Person> PersonService::update_person(const Person& draft) {
  Person normalized = draft;
  normalize_ids(normalized);
  if (auto err = validate_person(normalized, repo_); err) return err;
  // 乐观锁读-改-写与 INSERT 同受 WAL 写锁竞争影响，同一重试口径。
  // 注意重试必须整段重来（重读 rev），不能只重试 UPDATE 语句。
  return retry_on_locked([this, &normalized] {
    return repo_.update(normalized);
  });
}

Result<void> PersonService::remove_person(const std::string& id) {
  // 预检与删除同一写事务，避免并发插入引用造成 TOCTOU 孤儿
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  // 有人以此人为父母时，外键会拦住删除（SQLite foreign_keys=ON）。
  // 这里是有意的：静默级联删掉一整枝子孙，比明确报错危险得多。
  // 但人物级事实（事件/迁徙）只关于此人本身——留在库里是悬挂的
  // 幽灵记录（event.subject_id 无外键，删除不会报错），随主体一并
  // 删除并已在 spec 的 DELETE /persons/{id} 描述中注明。
  //
  // ROADMAP M7.2：删除被引事实时 409。人物本身被 citation 引用（字段级
  // 证据），或其即将级联删除的事件被引用——都不许删，先解除引用。
  // 引用计数需要在级联范围确定之后统计，故先取事件列表再判。
  // 预检与下面的删除循环同口径（全量游标翻页）：此前预检只看第一页
  // 1000 条，第 1001 条起被引用的事件会被级联删除、留下孤儿 citation
  // （第三轮新-6）。
  EventRepository citations_scope_events(*db_);
  CitationRepository citations(*db_);
  auto person_cited =
      citations.count_for_subject(CitationSubjectType::Person, id);
  if (!person_cited) return person_cited.error();
  long cited = *person_cited;
  std::optional<std::string> scope_cursor;
  for (;;) {
    EventFilter scope_ef;
    scope_ef.subject_type = EventSubjectType::Person;
    scope_ef.subject_id = id;
    scope_ef.limit = 1000;
    scope_ef.cursor = scope_cursor;
    auto scope_events = citations_scope_events.list(scope_ef);
    if (!scope_events) return scope_events.error();
    if (scope_events->empty()) break;
    for (const auto& e : *scope_events) {
      auto event_cited =
          citations.count_for_subject(CitationSubjectType::Event, e.id);
      if (!event_cited) return event_cited.error();
      cited += *event_cited;
    }
    scope_cursor = scope_events->back().id;
  }
  // 即将级联删除的迁徙记录也要计入证据引用——MigrationService::remove_migration
  // 有 409 保护，这里若漏掉会删掉被 citation 引用的迁徙，留下孤儿证据（H4）。
  MigrationRepository cited_migrations(*db_);
  std::optional<std::string> mg_pre_cursor;
  for (;;) {
    MigrationFilter mf;
    mf.subject_type = MigrationSubjectType::Person;
    mf.subject_id = id;
    mf.limit = 1000;
    mf.cursor = mg_pre_cursor;
    auto mgs = cited_migrations.list(mf);
    if (!mgs) return mgs.error();
    if (mgs->empty()) break;
    for (const auto& m : *mgs) {
      auto migration_cited =
          citations.count_for_subject(CitationSubjectType::Migration, m.id);
      if (!migration_cited) return migration_cited.error();
      cited += *migration_cited;
    }
    mg_pre_cursor = mgs->back().id;
  }
  if (cited > 0) {
    return conflict("人物（含其事件）被 " + std::to_string(cited) +
                    " 条证据引用，先解除引用再删除");
  }

  // 亲属/结合/支派引用必须先解除（2026-11 静态审查 H12）：否则最后
  // 一步 DELETE 撞外键返回 500——这里给出人话 409 和逐项计数。
  auto refs = repo_.count_relation_references(id);
  if (!refs) return refs.error();
  if (refs->parent_child_links > 0 || refs->union_memberships > 0 ||
      refs->founded_branches > 0 || refs->child_refs > 0) {
    return conflict("人物仍有未解除的关系（亲子 " +
                    std::to_string(refs->parent_child_links) + " 条、结合成员 " +
                    std::to_string(refs->union_memberships) + " 条、创建支派 " +
                    std::to_string(refs->founded_branches) + " 个、子女列引用 " +
                    std::to_string(refs->child_refs) +
                    " 条），先解除关系再删除");
  }

  // 三个删除必须同一事务：union_member.person_id 对 person 有外键，
  // 人物在结合中时最后一步 repo_.remove 会 FK 失败——没有事务的话
  // 事件/迁徙已经删掉，留下「人还在、事实没了」的部分删除。
  // Transaction 嵌套感知：外层已有事务时本层为空操作。
  // 翻页删到尽：单页上限不是总量上限，一次取一大页会让超过它的
  // 记录留下半截幽灵。游标 = 本页最后一条的 id（删完再取下一页）。
  EventRepository events(*db_);
  std::optional<std::string> ev_cursor;
  for (;;) {
    EventFilter ef;
    ef.subject_type = EventSubjectType::Person;
    ef.subject_id = id;
    ef.limit = 1000;
    ef.cursor = ev_cursor;
    auto evs = events.list(ef);
    if (!evs) return evs.error();
    if (evs->empty()) break;
    for (const auto& e : *evs) {
      if (auto r = events.remove(e.id); !r) return r;
    }
    ev_cursor = evs->back().id;
  }
  MigrationRepository migrations(*db_);
  std::optional<std::string> mg_cursor;
  for (;;) {
    MigrationFilter mf;
    mf.subject_type = MigrationSubjectType::Person;
    mf.subject_id = id;
    mf.limit = 1000;
    mf.cursor = mg_cursor;
    auto mgrs = migrations.list(mf);
    if (!mgrs) return mgrs.error();
    if (mgrs->empty()) break;
    for (const auto& m : *mgrs) {
      if (auto r = migrations.remove(m.id); !r) return r;
    }
    mg_cursor = mgrs->back().id;
  }
  // 附件随人删除（第四轮 P2-4）：attachment.subject_id 无外键，人删后
  // 行与内容文件都是悬挂幽灵。本人的头像引用在服务内先清，他人引用会
  // 以 409 拦截。与上面的事件/迁徙清理同事务。内容文件在提交后回收
  // （第五轮 P2-1：文件删除不可回滚）。
  std::vector<std::string> reclaim_shas;
  {
    AttachmentService attachments(*db_);
    auto ra = attachments.remove_subject_attachments("person", id);
    if (!ra) return ra.error();
    reclaim_shas = std::move(*ra);
  }
  // 名号行随人删除（person_name.person_id 外键指向 person）
  if (auto rn = repo_.remove_names(id); !rn) return rn;
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c;
  if (!reclaim_shas.empty()) {
    AttachmentService attachments(*db_);
    attachments.reclaim_content_files(reclaim_shas);
  }
  return {};
}

Result<std::vector<Person>> PersonService::search_persons(
    std::string_view keyword, int limit, const std::string& after_id) {
  return repo_.search(keyword, limit, after_id);
}

}  // namespace genealogy
