#include <genealogy/repository/event_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

using repo_internal::Stmt;
using repo_internal::bind_event;
using repo_internal::bind_opt_date;
using repo_internal::event_insert_sql;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_date;
using repo_internal::col_opt_text;
using repo_internal::col_text;

constexpr const char* kCols =
    "id, subject_type, subject_id, type, date, place_id, description,"
    " created_at";

enum Col {
  kId = 0,
  kSubjectType,
  kSubjectId,
  kType,
  kDate,
  kPlaceId,
  kDescription,
  kCreatedAt,
};

Event row_to_event(sqlite3_stmt* st) {
  Event e;
  e.id = col_text(st, kId);
  e.subject_type = event_subject_from_string(col_text(st, kSubjectType))
                       .value_or(EventSubjectType::Person);
  e.subject_id = col_text(st, kSubjectId);
  e.type = event_type_from_string(col_text(st, kType)).value_or(EventType::Annotation);
  e.date = col_opt_date(st, kDate);
  e.place_id = col_opt_text(st, kPlaceId);
  e.description = col_opt_text(st, kDescription);
  e.created_at = col_text(st, kCreatedAt);
  return e;
}

}  // namespace

EventRepository::EventRepository(Database& db) : db_(db) {}

Result<std::vector<Event>> EventRepository::list(
    const EventFilter& filter) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM event"
                          " WHERE (?1 IS NULL OR subject_type = ?1)"
                          "   AND (?2 IS NULL OR subject_id = ?2)"
                          "   AND (?3 IS NULL OR type = ?3)"
                          "   AND (?4 IS NULL OR id > ?4)"
                          " ORDER BY id LIMIT ?5";
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
  if (filter.type) {
    sqlite3_bind_text(st.get(), 3, to_string(*filter.type).c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st.get(), 3);
  }
  bind_opt_text(st.get(), 4, filter.cursor);
  sqlite3_bind_int(st.get(), 5, filter.limit > 0 ? filter.limit : 50);

  std::vector<Event> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_event(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Event> EventRepository::get(const std::string& id) const {
  const std::string sql =
      std::string("SELECT ").append(kCols) + " FROM event WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_event(st.get());
  return not_found("事件不存在: " + id);
}

Result<Event> EventRepository::insert(Event event) {
  if (event.id.empty()) event.id = generate_ulid();
  event.created_at = utc_now_iso();
  Stmt st(db_.raw(), event_insert_sql());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备 INSERT 失败: ") + sqlite3_errmsg(db_.raw())};
  }
  bind_event(st.get(), event);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return event;
}

Result<void> EventRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM event WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) return not_found("事件不存在: " + id);
  return {};
}

}  // namespace genealogy
