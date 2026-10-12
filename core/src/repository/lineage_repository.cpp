#include <genealogy/repository/lineage_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

using repo_internal::Stmt;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_int64;
using repo_internal::col_opt_text;
using repo_internal::col_text;

constexpr const char* kCols =
    "id, name, surname, surname_origin, commandery, hall_name,"
    " generation_poem, note, rev, created_at, updated_at";

enum Col {
  kId = 0,
  kName,
  kSurname,
  kSurnameOrigin,
  kCommandery,
  kHallName,
  kGenerationPoem,
  kNote,
  kRev,
  kCreatedAt,
  kUpdatedAt,
};

Lineage row_to_lineage(sqlite3_stmt* st) {
  Lineage l;
  l.id = col_text(st, kId);
  l.name = col_text(st, kName);
  l.surname = col_text(st, kSurname);
  l.surname_origin = col_opt_text(st, kSurnameOrigin);
  l.commandery = col_opt_text(st, kCommandery);
  l.hall_name = col_opt_text(st, kHallName);
  l.generation_poem = col_opt_text(st, kGenerationPoem);
  l.note = col_opt_text(st, kNote);
  l.rev = col_opt_int64(st, kRev).value_or(1);
  l.created_at = col_text(st, kCreatedAt);
  l.updated_at = col_text(st, kUpdatedAt);
  return l;
}

void bind_lineage(sqlite3_stmt* st, const Lineage& l) {
  int i = 1;
  sqlite3_bind_text(st, i++, l.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, l.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, l.surname.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, l.surname_origin);
  bind_opt_text(st, i++, l.commandery);
  bind_opt_text(st, i++, l.hall_name);
  bind_opt_text(st, i++, l.generation_poem);
  bind_opt_text(st, i++, l.note);
  sqlite3_bind_int64(st, i++, static_cast<std::int64_t>(l.rev));
  sqlite3_bind_text(st, i++, l.created_at.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, l.updated_at.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

LineageRepository::LineageRepository(Database& db) : db_(db) {}

Result<std::vector<Lineage>> LineageRepository::list(
    const LineageFilter& filter) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM lineage"
                          " WHERE (?1 IS NULL OR surname = ?1)"
                          "   AND (?2 IS NULL OR id > ?2)"
                          " ORDER BY id LIMIT ?3";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_opt_text(st.get(), 1, filter.surname);
  bind_opt_text(st.get(), 2, filter.cursor);
  sqlite3_bind_int(st.get(), 3, filter.limit > 0 ? filter.limit : 50);

  std::vector<Lineage> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_lineage(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Lineage> LineageRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kCols) + " FROM lineage WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_lineage(st.get());
  return not_found("宗族不存在: " + id);
}

Result<Lineage> LineageRepository::insert(Lineage lineage) {
  if (lineage.id.empty()) lineage.id = generate_ulid();
  lineage.rev = 1;
  lineage.created_at = utc_now_iso();
  lineage.updated_at = lineage.created_at;

  const std::string sql = std::string("INSERT INTO lineage(").append(kCols) +
                          ") VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 INSERT 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_lineage(st.get(), lineage);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return lineage;
}

Result<Lineage> LineageRepository::update(const Lineage& lineage) {
  auto current = get(lineage.id);
  if (!current) return current.error();
  if (current->rev != lineage.rev) {
    return conflict("版本冲突: 提交 rev=" + std::to_string(lineage.rev) +
                    "，库中 rev=" + std::to_string(current->rev));
  }

  Stmt st(db_.raw(),
          "UPDATE lineage SET"
          " name=?1, surname=?2, surname_origin=?3, commandery=?4,"
          " hall_name=?5, generation_poem=?6, note=?7, updated_at=?8,"
          " rev=rev+1 WHERE id=?9 AND rev=?10");
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 UPDATE 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  int i = 1;
  sqlite3_bind_text(st.get(), i++, lineage.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, lineage.surname.c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_text(st.get(), i++, lineage.surname_origin);
  bind_opt_text(st.get(), i++, lineage.commandery);
  bind_opt_text(st.get(), i++, lineage.hall_name);
  bind_opt_text(st.get(), i++, lineage.generation_poem);
  bind_opt_text(st.get(), i++, lineage.note);
  const std::string now = utc_now_iso();
  sqlite3_bind_text(st.get(), i++, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, lineage.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st.get(), i++, static_cast<std::int64_t>(lineage.rev));

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  Lineage updated = lineage;
  updated.rev = lineage.rev + 1;
  updated.updated_at = now;
  return updated;
}

Result<void> LineageRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM lineage WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) return not_found("宗族不存在: " + id);
  return {};
}

}  // namespace genealogy
