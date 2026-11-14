#include <genealogy/repository/branch_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

using repo_internal::Stmt;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_int64;
using repo_internal::col_opt_text;
using repo_internal::col_text;

constexpr const char* kCols =
    "id, lineage_id, name, founder_person_id, parent_branch_id,"
    " generation_char, note, rev";

enum Col {
  kId = 0,
  kLineageId,
  kName,
  kFounderPersonId,
  kParentBranchId,
  kGenerationChar,
  kNote,
  kRev,
};

Branch row_to_branch(sqlite3_stmt* st) {
  Branch b;
  b.id = col_text(st, kId);
  b.lineage_id = col_text(st, kLineageId);
  b.name = col_text(st, kName);
  b.founder_person_id = col_opt_text(st, kFounderPersonId);
  b.parent_branch_id = col_opt_text(st, kParentBranchId);
  b.generation_char = col_opt_text(st, kGenerationChar);
  b.note = col_opt_text(st, kNote);
  b.rev = col_opt_int64(st, kRev).value_or(1);
  return b;
}

void bind_branch(sqlite3_stmt* st, const Branch& b) {
  int i = 1;
  sqlite3_bind_text(st, i++, b.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, b.lineage_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, b.name.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, b.founder_person_id);
  bind_opt_text(st, i++, b.parent_branch_id);
  bind_opt_text(st, i++, b.generation_char);
  bind_opt_text(st, i++, b.note);
  sqlite3_bind_int64(st, i++, static_cast<std::int64_t>(b.rev));
}

}  // namespace

BranchRepository::BranchRepository(Database& db) : db_(db) {}

Result<std::vector<Branch>> BranchRepository::list(
    const BranchFilter& filter) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM branch"
                          " WHERE (?1 IS NULL OR lineage_id = ?1)"
                          "   AND (?2 IS NULL OR id > ?2)"
                          " ORDER BY id LIMIT ?3";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_opt_text(st.get(), 1, filter.lineage_id);
  bind_opt_text(st.get(), 2, filter.cursor);
  sqlite3_bind_int(st.get(), 3, filter.limit > 0 ? filter.limit : 50);

  std::vector<Branch> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_branch(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Branch> BranchRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kCols) + " FROM branch WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_branch(st.get());
  return not_found("支派不存在: " + id);
}

Result<Branch> BranchRepository::insert(Branch branch) {
  if (branch.id.empty()) branch.id = generate_ulid();
  branch.rev = 1;
  const std::string sql = std::string("INSERT INTO branch(").append(kCols) +
                          ") VALUES (?1,?2,?3,?4,?5,?6,?7,?8)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 INSERT 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_branch(st.get(), branch);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return branch;
}

Result<Branch> BranchRepository::update(const Branch& branch) {
  auto current = get(branch.id);
  if (!current) return current.error();
  if (current->rev != branch.rev) {
    return conflict("版本冲突: 提交 rev=" + std::to_string(branch.rev) +
                    "，库中 rev=" + std::to_string(current->rev));
  }

  Stmt st(db_.raw(),
          "UPDATE branch SET"
          " lineage_id=?1, name=?2, founder_person_id=?3, parent_branch_id=?4,"
          " generation_char=?5, note=?6, rev=rev+1"
          " WHERE id=?7 AND rev=?8");
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 UPDATE 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  int i = 1;
  sqlite3_bind_text(st.get(), i++, branch.lineage_id.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, branch.name.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st.get(), i++, branch.founder_person_id);
  bind_opt_text(st.get(), i++, branch.parent_branch_id);
  bind_opt_text(st.get(), i++, branch.generation_char);
  bind_opt_text(st.get(), i++, branch.note);
  sqlite3_bind_text(st.get(), i++, branch.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st.get(), i++, static_cast<std::int64_t>(branch.rev));

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  // get() 预检与 UPDATE 之间可能被别的连接抢先写（rev 变了）：
  // 匹配 0 行必须报 Conflict，不能当成功虚报 rev（同 person_repository）。
  if (sqlite3_changes(db_.raw()) == 0) {
    auto now_current = get(branch.id);
    const std::int64_t cur =
        now_current ? static_cast<std::int64_t>(now_current->rev) : -1;
    return conflict("版本冲突: 提交 rev=" + std::to_string(branch.rev) +
                    "，库中 rev=" + std::to_string(cur));
  }
  Branch updated = branch;
  updated.rev = branch.rev + 1;
  return updated;
}

Result<void> BranchRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM branch WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) return not_found("支派不存在: " + id);
  return {};
}

}  // namespace genealogy
