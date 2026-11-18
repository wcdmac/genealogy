#include <genealogy/service/person_service.hpp>

#include <string>

#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/migration_repository.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/ulid.hpp>

namespace genealogy {

PersonService::PersonService(Database& db)
    : db_(&db), repo_(db), lineages_(db) {}

namespace {

// 建档与更新共用的领域校验。空 Error 表示通过。
Error validate_person(const Person& draft, LineageRepository& lineages,
                      PersonRepository& repo) {
  if (draft.lineage_id.empty()) return invalid_argument("lineage_id 必填");
  if (draft.family_name.empty()) return invalid_argument("姓必填");
  if (draft.given_name.empty()) return invalid_argument("名必填");

  // 宗族存在性：未传 --lineage 时 CLI 落入占位宗族（bootstrap 保证存在）
  auto lineage = lineages.get(draft.lineage_id);
  if (!lineage) return not_found("宗族不存在: " + draft.lineage_id);

  Person p = draft;
  // 空串 ID 视为未设置（CLI 传 --father "" 等场景），
  // 否则会作为无效外键值写入库中触发 FOREIGN KEY 失败。
  if (p.father_id && p.father_id->empty()) p.father_id.reset();
  if (p.mother_id && p.mother_id->empty()) p.mother_id.reset();
  if (p.branch_id && p.branch_id->empty()) p.branch_id.reset();

  // 父母存在性（拟制关系在 M1 与 parent_child 对齐后扩展）
  if (p.father_id && !p.father_id->empty()) {
    if (!is_valid_ulid(*p.father_id)) {
      return invalid_argument("father_id 不是合法 ULID: " + *p.father_id);
    }
    auto father = repo.get(*p.father_id);
    if (!father) return not_found("父亲不存在: " + *p.father_id);
  }
  if (p.mother_id && !p.mother_id->empty()) {
    if (!is_valid_ulid(*p.mother_id)) {
      return invalid_argument("mother_id 不是合法 ULID: " + *p.mother_id);
    }
    auto mother = repo.get(*p.mother_id);
    if (!mother) return not_found("母亲不存在: " + *p.mother_id);
  }
  return {};
}

}  // namespace

Result<Person> PersonService::create_person(const Person& draft) {
  if (auto err = validate_person(draft, lineages_, repo_); err) return err;
  return repo_.insert(draft);
}

Result<Person> PersonService::get_person(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Person>> PersonService::list_persons(
    const PersonFilter& filter) {
  return repo_.list(filter);
}

Result<Person> PersonService::update_person(const Person& p) {
  if (auto err = validate_person(p, lineages_, repo_); err) return err;
  return repo_.update(p);
}

Result<void> PersonService::remove_person(const std::string& id) {
  // 有人以此人为父母时，外键会拦住删除（SQLite foreign_keys=ON）。
  // 这里是有意的：静默级联删掉一整枝子孙，比明确报错危险得多。
  // 但人物级事实（事件/迁徙）只关于此人本身——留在库里是悬挂的
  // 幽灵记录（event.subject_id 无外键，删除不会报错），随主体一并
  // 删除并已在 spec 的 DELETE /persons/{id} 描述中注明。
  //
  // ROADMAP M7.2：删除被引事实时 409。人物本身被 citation 引用（字段级
  // 证据），或其即将级联删除的事件被引用——都不许删，先解除引用。
  // 引用计数需要在级联范围确定之后统计，故先取事件列表再判。
  EventRepository citations_scope_events(*db_);
  EventFilter scope_ef;
  scope_ef.subject_type = EventSubjectType::Person;
  scope_ef.subject_id = id;
  scope_ef.limit = 1000;
  auto scope_events = citations_scope_events.list(scope_ef);
  if (!scope_events) return scope_events.error();

  CitationRepository citations(*db_);
  auto person_cited =
      citations.count_for_subject(CitationSubjectType::Person, id);
  if (!person_cited) return person_cited.error();
  long cited = *person_cited;
  for (const auto& e : *scope_events) {
    auto event_cited =
        citations.count_for_subject(CitationSubjectType::Event, e.id);
    if (!event_cited) return event_cited.error();
    cited += *event_cited;
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
  Transaction tx(*db_);
  if (tx.error()) return *tx.error();
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
  // 名号行随人删除（person_name.person_id 外键指向 person）
  if (auto rn = repo_.remove_names(id); !rn) return rn;
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c;
  return {};
}

Result<std::vector<Person>> PersonService::search_persons(
    std::string_view keyword, int limit) {
  return repo_.search(keyword, limit);
}

}  // namespace genealogy
