#include <genealogy/repository/citation_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

namespace genealogy {

namespace {

using repo_internal::bind_opt_text;
using repo_internal::col_opt_text;
using repo_internal::col_text;
using repo_internal::Stmt;

constexpr const char* kCols =
    "id, subject_type, subject_id, source_id, locator, confidence, note";

enum Col {
  kId = 0,
  kSubjectType,
  kSubjectId,
  kSourceId,
  kLocator,
  kConfidence,
  kNote,
};

Citation row_to_citation(sqlite3_stmt* st) {
  Citation c;
  c.id = col_text(st, kId);
  c.subject_type = citation_subject_from_string(col_text(st, kSubjectType))
                       .value_or(CitationSubjectType::Person);
  c.subject_id = col_text(st, kSubjectId);
  c.source_id = col_text(st, kSourceId);
  c.locator = col_opt_text(st, kLocator);
  c.confidence = confidence_from_string(col_text(st, kConfidence))
                     .value_or(Confidence::Medium);
  c.note = col_opt_text(st, kNote);
  return c;
}

void bind_citation(sqlite3_stmt* st, const Citation& c) {
  int i = 1;
  sqlite3_bind_text(st, i++, c.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(c.subject_type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, c.subject_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, c.source_id.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, c.locator);
  sqlite3_bind_text(st, i++, to_string(c.confidence).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_text(st, i++, c.note);
}

}  // namespace

CitationRepository::CitationRepository(Database& db) : db_(db) {}

Result<std::vector<Citation>> CitationRepository::list(
    const CitationFilter& filter) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM citation"
                          " WHERE (?1 IS NULL OR subject_type = ?1)"
                          "   AND (?2 IS NULL OR subject_id = ?2)"
                          "   AND (?3 IS NULL OR source_id = ?3)"
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
  bind_opt_text(st.get(), 3, filter.source_id);
  bind_opt_text(st.get(), 4, filter.cursor);
  sqlite3_bind_int(st.get(), 5, filter.limit > 0 ? filter.limit : 50);

  std::vector<Citation> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_citation(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Citation> CitationRepository::get(const std::string& id) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM citation WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) return row_to_citation(st.get());
  return not_found("证据引用不存在: " + id);
}

Result<Citation> CitationRepository::insert(Citation c) {
  if (c.id.empty()) c.id = generate_ulid();

  const std::string sql =
      "INSERT INTO citation"
      " (id, subject_type, subject_id, source_id, locator, confidence, note)"
      " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 INSERT 失败");
  bind_citation(st.get(), c);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return c;
}

Result<Citation> CitationRepository::update(const Citation& c) {
  const std::string sql =
      "UPDATE citation SET"
      " subject_type=?1, subject_id=?2, source_id=?3, locator=?4,"
      " confidence=?5, note=?6"
      " WHERE id=?7";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 UPDATE 失败");
  // 不能复用 bind_citation——那是 INSERT 的参数序（id=?1..note=?7），
  // UPDATE 的占位序不同（subject_type=?1..id=?7）；M7.2 曾因此让
  // PATCH 引用直接违反约束 500（索引与列错位），test_http 的
  // 「挂证据→R16 消失→R17 裁定」用例补上了这个缺口。
  sqlite3_bind_text(st.get(), 1, to_string(c.subject_type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 2, c.subject_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 3, c.source_id.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st.get(), 4, c.locator);
  sqlite3_bind_text(st.get(), 5, to_string(c.confidence).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_opt_text(st.get(), 6, c.note);
  sqlite3_bind_text(st.get(), 7, c.id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("证据引用不存在: " + c.id);
  }
  return c;
}

Result<void> CitationRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM citation WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("证据引用不存在: " + id);
  }
  return {};
}

int CitationRepository::count_for_subject(
    CitationSubjectType type, const std::string& subject_id) const {
  Stmt st(db_.raw(),
          "SELECT COUNT(*) FROM citation WHERE subject_type = ?1"
          " AND subject_id = ?2");
  if (!st.ok()) return 0;
  sqlite3_bind_text(st.get(), 1, to_string(type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 2, subject_id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) {
    return sqlite3_column_int(st.get(), 0);
  }
  return 0;
}

int CitationRepository::count_for_source(const std::string& source_id) const {
  Stmt st(db_.raw(),
          "SELECT COUNT(*) FROM citation WHERE source_id = ?1");
  if (!st.ok()) return 0;
  sqlite3_bind_text(st.get(), 1, source_id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) {
    return sqlite3_column_int(st.get(), 0);
  }
  return 0;
}

int CitationRepository::count_subjects_without_citation(
    CitationSubjectType type) const {
  // NOT EXISTS 走 idx_citation_subject(subject_type, subject_id)，
  // 20 万级主体全表反查无压力；返回值与 count_for_subject 同为
  // int——超 21 亿引用才会溢出，先不设防。
  const char* table = nullptr;
  switch (type) {
    case CitationSubjectType::Person: table = "person"; break;
    case CitationSubjectType::Event: table = "event"; break;
    default: return 0;  // union/migration/branch/lineage 无独立主体表口径
  }
  std::string sql = std::string("SELECT COUNT(*) FROM ") + table +
                    " WHERE NOT EXISTS (SELECT 1 FROM citation WHERE "
                    "citation.subject_type = ?1 AND "
                    "citation.subject_id = " +
                    table + ".id)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return 0;
  sqlite3_bind_text(st.get(), 1, to_string(type).c_str(), -1,
                    SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) == SQLITE_ROW) {
    return sqlite3_column_int(st.get(), 0);
  }
  return 0;
}

}  // namespace genealogy
