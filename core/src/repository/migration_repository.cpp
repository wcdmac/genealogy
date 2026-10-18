#include <genealogy/repository/migration_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

using repo_internal::Stmt;
using repo_internal::bind_opt_date;
using repo_internal::bind_opt_text;
using repo_internal::col_date;
using repo_internal::col_opt_date;
using repo_internal::col_opt_text;
using repo_internal::col_text;

constexpr const char* kCols =
    "id, subject_type, subject_id, from_place_id, to_place_id, date,"
    " reason, route, note";

enum Col {
  kId = 0,
  kSubjectType,
  kSubjectId,
  kFromPlaceId,
  kToPlaceId,
  kDate,
  kReason,
  kRoute,
  kNote,
};

Migration row_to_migration(sqlite3_stmt* st) {
  Migration m;
  m.id = col_text(st, kId);
  m.subject_type =
      migration_subject_from_string(col_text(st, kSubjectType))
          .value_or(MigrationSubjectType::Person);
  m.subject_id = col_text(st, kSubjectId);
  m.from_place_id = col_opt_text(st, kFromPlaceId);
  m.to_place_id = col_opt_text(st, kToPlaceId);
  // date 列存 GenealogyDate 的 JSON 串，按对象读回（与 event 同口径）
  m.date = col_opt_date(st, kDate);
  m.reason = col_opt_text(st, kReason);
  m.route = col_opt_text(st, kRoute);
  m.note = col_opt_text(st, kNote);
  return m;
}

}  // namespace

MigrationRepository::MigrationRepository(Database& db) : db_(db) {}

Result<std::vector<Migration>> MigrationRepository::list(
    const MigrationFilter& filter) const {
  // subject_id 与 branch_id 是或的关系：branch_id 命中支派自身的迁徙，
  // 也命中其成员的迁徙——后者由 REST 层展开成员 ID 后多次查询合并
  // （仓储不 JOIN person，迁徙查询按 subject 精确匹配即可）。
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM migration"
                          " WHERE (?1 IS NULL OR subject_type = ?1)"
                          "   AND (?2 IS NULL OR subject_id = ?2)"
                          "   AND (?3 IS NULL OR id > ?3)"
                          " ORDER BY id LIMIT ?4";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (filter.subject_type) {
    sqlite3_bind_text(st.get(), 1, to_string(*filter.subject_type).c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st.get(), 1);
  }
  bind_opt_text(st.get(), 2, filter.subject_id);
  bind_opt_text(st.get(), 3, filter.cursor);
  sqlite3_bind_int(st.get(), 4, filter.limit > 0 ? filter.limit : 50);

  std::vector<Migration> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_migration(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Migration> MigrationRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kCols) + " FROM migration WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_migration(st.get());
  return not_found("迁徙记录不存在: " + id);
}

Result<Migration> MigrationRepository::insert(Migration migration) {
  if (migration.id.empty()) migration.id = generate_ulid();
  Stmt st(db_.raw(),
          "INSERT INTO migration (id, subject_type, subject_id,"
          " from_place_id, to_place_id, date, reason, route, note)"
          " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 INSERT 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, migration.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 2, to_string(migration.subject_type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 3, migration.subject_id.c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_text(st.get(), 4, migration.from_place_id);
  bind_opt_text(st.get(), 5, migration.to_place_id);
  bind_opt_date(st.get(), 6, migration.date);
  bind_opt_text(st.get(), 7, migration.reason);
  bind_opt_text(st.get(), 8, migration.route);
  bind_opt_text(st.get(), 9, migration.note);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    // 外键失败（subject/place 不存在）带原始信息：调用方要能看出
    // 是「人不存在」还是「地点不存在」
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return migration;
}

Result<Migration> MigrationRepository::update(const Migration& migration) {
  Stmt st(db_.raw(),
          "UPDATE migration SET subject_type=?1, subject_id=?2,"
          " from_place_id=?3, to_place_id=?4, date=?5, reason=?6,"
          " route=?7, note=?8 WHERE id=?9");
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 UPDATE 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, to_string(migration.subject_type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 2, migration.subject_id.c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_text(st.get(), 3, migration.from_place_id);
  bind_opt_text(st.get(), 4, migration.to_place_id);
  bind_opt_date(st.get(), 5, migration.date);
  bind_opt_text(st.get(), 6, migration.reason);
  bind_opt_text(st.get(), 7, migration.route);
  bind_opt_text(st.get(), 8, migration.note);
  sqlite3_bind_text(st.get(), 9, migration.id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("迁徙记录不存在: " + migration.id);
  }
  return migration;
}

Result<void> MigrationRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM migration WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("迁徙记录不存在: " + id);
  }
  return {};
}

}  // namespace genealogy
