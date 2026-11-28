#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/branch_service.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/event_repository.hpp>

#include <sqlite3.h>
#include <string>

namespace genealogy {

BranchService::BranchService(Database& db)
    : db_(&db), repo_(db), persons_(db) {}

Error BranchService::validate(const Branch& b) {
  if (b.name.empty()) return invalid_argument("支派名称必填");


  if (b.founder_person_id && !b.founder_person_id->empty()) {
    if (!is_valid_ulid(*b.founder_person_id)) {
      return invalid_argument("founder_person_id 不是合法 ULID: " +
                              *b.founder_person_id);
    }
    auto founder = persons_.get(*b.founder_person_id);
    if (!founder) return not_found("始迁祖不存在: " + *b.founder_person_id);
  }

  if (b.parent_branch_id && !b.parent_branch_id->empty()) {
    if (!is_valid_ulid(*b.parent_branch_id)) {
      return invalid_argument("parent_branch_id 不是合法 ULID: " +
                              *b.parent_branch_id);
    }
    auto parent = repo_.get(*b.parent_branch_id);
    if (!parent) return not_found("父房不存在: " + *b.parent_branch_id);
  }
  return {};
}

Result<Branch> BranchService::create_branch(const Branch& draft) {
  Branch normalized = draft;
  if (normalized.founder_person_id && normalized.founder_person_id->empty())
    normalized.founder_person_id.reset();
  if (normalized.parent_branch_id && normalized.parent_branch_id->empty())
    normalized.parent_branch_id.reset();
  if (auto err = validate(normalized); err) return err;
  return repo_.insert(normalized);
}

Result<Branch> BranchService::get_branch(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Branch>> BranchService::list_branches(
    const BranchFilter& filter) {
  return repo_.list(filter);
}

Result<Branch> BranchService::update_branch(const Branch& draft) {
  Branch normalized = draft;
  if (normalized.founder_person_id && normalized.founder_person_id->empty())
    normalized.founder_person_id.reset();
  if (normalized.parent_branch_id && normalized.parent_branch_id->empty())
    normalized.parent_branch_id.reset();
  const Branch& branch = normalized;
  if (branch.id.empty()) return invalid_argument("id 必填");
  if (auto err = validate(branch); err) return err;
  // 父子链环检测（第三轮新-1）：A→B、B→A 各自都能过上面的单步校验，
  // 但 GET /branches/{id}/tree 的递归展开会栈溢出。沿父链向上走，
  // 任何一步回到本支派即成环。
  if (branch.parent_branch_id && !branch.parent_branch_id->empty()) {
    std::string cursor = *branch.parent_branch_id;
    for (int guard = 0; guard < 10000; ++guard) {
      if (cursor == branch.id) {
        return invalid_argument("支派父子链成环（" + branch.id + " 的祖先链回到自身）");
      }
      auto cur = repo_.get(cursor);
      if (!cur) break;  // 链尾断链不在此处报错（validate 已保证存在性）
      if (!cur->parent_branch_id || cur->parent_branch_id->empty()) break;
      cursor = *cur->parent_branch_id;
    }
  }
  return repo_.update(branch);
}

Result<void> BranchService::remove_branch(const std::string& id) {
  // 预检与删除必须在同一写事务内，否则并发插入引用会留下孤儿（TOCTOU）。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  // ROADMAP M7.2：删除被引事实时 409——支派被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  auto cn = citations.count_for_subject(CitationSubjectType::Branch, id);
  if (!cn) return cn.error();
  const int n = *cn;
  if (n > 0) {
    return conflict("支派被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  // 人物归属/子支派引用（第四轮补口）：person.branch_id 与
  // branch.parent_branch_id 都有外键，不预检时删除会以 500（FK 原文）
  // 冒出，与其余服务的 409 口径不符。
  {
    const char* const kSqls[] = {
        "SELECT COUNT(*) FROM person WHERE branch_id=?1",
        "SELECT COUNT(*) FROM branch WHERE parent_branch_id=?1",
    };
    long members = 0, children = 0;
    for (int i = 0; i < 2; ++i) {
      sqlite3_stmt* st = nullptr;
      if (sqlite3_prepare_v2(db_->raw(), kSqls[i], -1, &st, nullptr) !=
          SQLITE_OK)
        return Error{ErrorCode::IoError, "支派引用计数查询准备失败"};
      sqlite3_bind_text(st, 1, id.c_str(),
                        static_cast<int>(id.size()), SQLITE_TRANSIENT);
      const bool row = sqlite3_step(st) == SQLITE_ROW;
      const long v = row ? sqlite3_column_int64(st, 0) : -1;
      const std::string err = row ? "" : sqlite3_errmsg(db_->raw());
      sqlite3_finalize(st);
      if (!row) return Error{ErrorCode::Internal, "支派引用计数查询失败: " + err};
      (i == 0 ? members : children) = v;
    }
    if (members > 0 || children > 0) {
      return conflict("支派仍被引用（归属人物 " + std::to_string(members) +
                      " 人、子支派 " + std::to_string(children) +
                      " 个），先转移归属再删除");
    }
  }
  // 以支派为主体的事件随支派删除（第四轮 P2-6，union 路径同口径）。
  {
    EventRepository events(*db_);
    std::optional<std::string> ev_cursor;
    for (;;) {
      EventFilter ef;
      ef.subject_type = EventSubjectType::Branch;
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
  }
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
