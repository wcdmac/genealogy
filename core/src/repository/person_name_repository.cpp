#include <genealogy/repository/person_name_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

namespace genealogy {

namespace {

using repo_internal::bind_opt_date;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_date;
using repo_internal::col_opt_text;
using repo_internal::col_text;
using repo_internal::Stmt;

constexpr const char* kCols =
    "id, person_id, name, name_type, start_date, end_date, is_primary, note";

enum Col {
  kId = 0,
  kPersonId,
  kName,
  kType,
  kStartDate,
  kEndDate,
  kIsPrimary,
  kNote,
};

PersonName row_to_person_name(sqlite3_stmt* st) {
  PersonName n;
  n.id = col_text(st, kId);
  n.person_id = col_text(st, kPersonId);
  n.name = col_text(st, kName);
  n.type = person_name_type_from_string(col_text(st, kType))
               .value_or(PersonNameType::Genealogy);
  n.start_date = col_opt_date(st, kStartDate);
  n.end_date = col_opt_date(st, kEndDate);
  n.is_primary = sqlite3_column_int(st, kIsPrimary) != 0;
  n.note = col_opt_text(st, kNote);
  return n;
}

}  // namespace

PersonNameRepository::PersonNameRepository(Database& db) : db_(db) {}

Result<std::vector<PersonName>> PersonNameRepository::list(
    const std::string& person_id) const {
  if (person_id.empty()) return invalid_argument("person_id 必填");
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM person_name WHERE person_id = ?1"
                          " ORDER BY is_primary DESC, id";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, person_id.c_str(), -1, SQLITE_TRANSIENT);
  std::vector<PersonName> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_person_name(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<PersonName> PersonNameRepository::get(const std::string& id) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM person_name WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_person_name(st.get());
  return not_found("名号不存在: " + id);
}

Result<PersonName> PersonNameRepository::insert(const PersonName& n) {
  if (n.id.empty()) {
    // caller 需要在置主之前拿到 id（clear_primary 的 except_id 用），
    // 这里不再代生成——服务层生成后传入
    return invalid_argument("名号 id 必须由服务层预生成");
  }
  const std::string sql =
      "INSERT INTO person_name"
      " (id, person_id, name, name_type, start_date, end_date, is_primary, note)"
      " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 INSERT 失败");
  int i = 1;
  sqlite3_bind_text(st.get(), i++, n.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, n.person_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, n.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, to_string(n.type).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_date(st.get(), i++, n.start_date);
  bind_opt_date(st.get(), i++, n.end_date);
  sqlite3_bind_int(st.get(), i++, n.is_primary ? 1 : 0);
  bind_opt_text(st.get(), i++, n.note);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return n;
}

Result<void> PersonNameRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM person_name WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("名号不存在: " + id);
  }
  return {};
}

Result<void> PersonNameRepository::clear_primary(const std::string& person_id,
                                                 const std::string& except_id) {
  Stmt st(db_.raw(),
          "UPDATE person_name SET is_primary = 0"
          " WHERE person_id = ?1 AND is_primary = 1 AND id <> ?2");
  if (!st.ok()) return internal("准备 UPDATE 失败");
  sqlite3_bind_text(st.get(), 1, person_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 2, except_id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("置主失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return {};
}

}  // namespace genealogy
