#include <genealogy/repository/place_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

using repo_internal::bind_place;
using repo_internal::place_insert_sql;
using repo_internal::Stmt;
using repo_internal::bind_opt_double;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_double;
using repo_internal::col_opt_text;
using repo_internal::col_text;

constexpr const char* kCols =
    "id, name, type, modern_admin, historical_name, lat, lng, note";

enum Col {
  kId = 0,
  kName,
  kType,
  kModernAdmin,
  kHistoricalName,
  kLat,
  kLng,
  kNote,
};

Place row_to_place(sqlite3_stmt* st) {
  Place p;
  p.id = col_text(st, kId);
  p.name = col_text(st, kName);
  p.type = place_type_from_string(col_text(st, kType));
  p.modern_admin = col_opt_text(st, kModernAdmin);
  p.historical_name = col_opt_text(st, kHistoricalName);
  p.lat = col_opt_double(st, kLat);
  p.lng = col_opt_double(st, kLng);
  p.note = col_opt_text(st, kNote);
  return p;
}

}  // namespace

PlaceRepository::PlaceRepository(Database& db) : db_(db) {}

Result<std::vector<Place>> PlaceRepository::list(
    const PlaceFilter& filter) const {
  // name_contains 用 ESCAPE 转义 LIKE 通配符，避免用户输入 %/_ 被当通配符
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM place"
                          " WHERE (?1 IS NULL OR type = ?1)"
                          "   AND (?2 IS NULL OR (name LIKE ?2 ESCAPE '\\'"
                          "        OR modern_admin LIKE ?2 ESCAPE '\\'))"
                          "   AND (?3 IS NULL OR id > ?3)"
                          " ORDER BY id LIMIT ?4";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (filter.type) {
    sqlite3_bind_text(st.get(), 1, to_string(*filter.type).c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st.get(), 1);
  }
  if (filter.name_contains && !filter.name_contains->empty()) {
    // 转义 %、_、\ 后包上 %...%
    std::string pat = "%";
    for (const char c : *filter.name_contains) {
      if (c == '%' || c == '_' || c == '\\') pat += '\\';
      pat += c;
    }
    pat += '%';
    sqlite3_bind_text(st.get(), 2, pat.c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st.get(), 2);
  }
  bind_opt_text(st.get(), 3, filter.cursor);
  sqlite3_bind_int(st.get(), 4, filter.limit > 0 ? filter.limit : 50);

  std::vector<Place> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_place(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Place> PlaceRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kCols) + " FROM place WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(st.get());
  if (rc == SQLITE_ROW) return row_to_place(st.get());
  if (rc != SQLITE_DONE) {
    return internal(std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw()));
  }
  return not_found("地点不存在: " + id);
}

Result<Place> PlaceRepository::insert(Place place) {
  if (place.id.empty()) place.id = generate_ulid();
  Stmt st(db_.raw(), place_insert_sql());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 INSERT 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_place(st.get(), place);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return place;
}

Result<Place> PlaceRepository::update(const Place& place) {
  Stmt st(db_.raw(),
          "UPDATE place SET"
          " name=?1, type=?2, modern_admin=?3, historical_name=?4,"
          " lat=?5, lng=?6, note=?7 WHERE id=?8");
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 UPDATE 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  int i = 1;
  sqlite3_bind_text(st.get(), i++, place.name.c_str(), -1, SQLITE_TRANSIENT);
  if (place.type) {
    sqlite3_bind_text(st.get(), i++, to_string(*place.type).c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st.get(), i++);
  }
  bind_opt_text(st.get(), i++, place.modern_admin);
  bind_opt_text(st.get(), i++, place.historical_name);
  bind_opt_double(st.get(), i++, place.lat);
  bind_opt_double(st.get(), i++, place.lng);
  bind_opt_text(st.get(), i++, place.note);
  sqlite3_bind_text(st.get(), i++, place.id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) return not_found("地点不存在: " + place.id);
  return place;
}

Result<void> PlaceRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM place WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) return not_found("地点不存在: " + id);
  return {};
}

}  // namespace genealogy
