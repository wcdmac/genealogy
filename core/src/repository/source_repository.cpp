#include <genealogy/repository/source_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

#include "sqlite_util.hpp"

namespace genealogy {

namespace {

using repo_internal::bind_opt_text;
using repo_internal::col_opt_text;
using repo_internal::col_text;
using repo_internal::Stmt;

constexpr const char* kCols =
    "id, type, title, author, year_text, repository, uri, citation, note";

enum Col {
  kId = 0,
  kType,
  kTitle,
  kAuthor,
  kYearText,
  kRepository,
  kUri,
  kCitation,
  kNote,
};

Source row_to_source(sqlite3_stmt* st) {
  Source src;
  src.id = col_text(st, kId);
  src.type = source_type_from_string(col_text(st, kType))
                 .value_or(SourceType::Other);
  src.title = col_text(st, kTitle);
  src.author = col_opt_text(st, kAuthor);
  src.year_text = col_opt_text(st, kYearText);
  src.repository = col_opt_text(st, kRepository);
  src.uri = col_opt_text(st, kUri);
  src.citation_text = col_opt_text(st, kCitation);
  src.note = col_opt_text(st, kNote);
  return src;
}

void bind_source(sqlite3_stmt* st, const Source& src) {
  int i = 1;
  sqlite3_bind_text(st, i++, src.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(src.type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, src.title.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, src.author);
  bind_opt_text(st, i++, src.year_text);
  bind_opt_text(st, i++, src.repository);
  bind_opt_text(st, i++, src.uri);
  bind_opt_text(st, i++, src.citation_text);
  bind_opt_text(st, i++, src.note);
}

}  // namespace

// LIKE 模式转义：与 document_repository 同纪律（2026-11 静态审查 L2——
// 搜 "100%"、"a_b" 曾误匹配）。\ % _ 全部转义，配 ESCAPE '\' 使用。
std::string like_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (const char c : s) {
    if (c == '\\' || c == '%' || c == '_') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

SourceRepository::SourceRepository(Database& db) : db_(db) {}

Result<std::vector<Source>> SourceRepository::list(
    const SourceFilter& filter) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM source"
                          " WHERE (?1 IS NULL OR type = ?1)"
                          "   AND (?2 IS NULL OR title LIKE '%' || like_escape(?2) || '%' ESCAPE '\\')"
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
  bind_opt_text(st.get(), 2, filter.title_contains);
  bind_opt_text(st.get(), 3, filter.cursor);
  sqlite3_bind_int(st.get(), 4, filter.limit > 0 ? filter.limit : 50);

  std::vector<Source> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_source(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Source> SourceRepository::get(const std::string& id) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM source WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(st.get());
  if (rc == SQLITE_ROW) return row_to_source(st.get());
  if (rc != SQLITE_DONE) {
    return internal(std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw()));
  }
  return not_found("来源不存在: " + id);
}

Result<Source> SourceRepository::insert(Source src) {
  if (src.id.empty()) src.id = generate_ulid();

  const std::string sql =
      "INSERT INTO source"
      " (id, type, title, author, year_text, repository, uri, citation, note)"
      " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 INSERT 失败");
  bind_source(st.get(), src);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return src;
}

Result<Source> SourceRepository::update(const Source& src) {
  // 无 rev（溯源元数据）：直接按 id 覆盖，changes()==0 即不存在
  const std::string sql =
      "UPDATE source SET"
      " type=?1, title=?2, author=?3, year_text=?4, repository=?5,"
      " uri=?6, citation=?7, note=?8"
      " WHERE id=?9";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 UPDATE 失败");
  int i = 1;
  sqlite3_bind_text(st.get(), i++, to_string(src.type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, src.title.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st.get(), i++, src.author);
  bind_opt_text(st.get(), i++, src.year_text);
  bind_opt_text(st.get(), i++, src.repository);
  bind_opt_text(st.get(), i++, src.uri);
  bind_opt_text(st.get(), i++, src.citation_text);
  bind_opt_text(st.get(), i++, src.note);
  sqlite3_bind_text(st.get(), i++, src.id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("来源不存在: " + src.id);
  }
  return src;
}

Result<void> SourceRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM source WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("来源不存在: " + id);
  }
  return {};
}

}  // namespace genealogy
