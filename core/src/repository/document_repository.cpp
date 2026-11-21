#include <genealogy/repository/document_repository.hpp>

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
using repo_internal::qualify_cols;
using repo_internal::quote_fts;
using repo_internal::Stmt;

constexpr const char* kCols =
    "id, type, title, author, date_text, edition, content, seq";

enum Col {
  kId = 0,
  kType,
  kTitle,
  kAuthor,
  kDateText,
  kEdition,
  kContent,
  kSeq,
};

Document row_to_document(sqlite3_stmt* st) {
  Document d;
  d.id = col_text(st, kId);
  d.type = document_type_from_string(col_text(st, kType))
               .value_or(DocumentType::Preface);
  d.title = col_text(st, kTitle);
  d.author = col_opt_text(st, kAuthor);
  d.date_text = col_opt_text(st, kDateText);
  d.edition = col_opt_text(st, kEdition);
  d.content = col_opt_text(st, kContent);
  if (sqlite3_column_type(st, kSeq) != SQLITE_NULL) {
    d.seq = sqlite3_column_int64(st, kSeq);
  }
  return d;
}

void bind_document(sqlite3_stmt* st, const Document& d) {
  int i = 1;
  sqlite3_bind_text(st, i++, d.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, to_string(d.type).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, i++, d.title.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st, i++, d.author);
  bind_opt_text(st, i++, d.date_text);
  bind_opt_text(st, i++, d.edition);
  bind_opt_text(st, i++, d.content);
  if (d.seq) {
    sqlite3_bind_int64(st, i++, *d.seq);
  } else {
    sqlite3_bind_null(st, i++);
  }
}

// unicode codepoint 计数（trigram MATCH 需要 ≥3 字符才算有效查询）
std::size_t codepoint_count(std::string_view s) {
  std::size_t n = 0;
  for (const char c : s) {
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
  }
  return n;
}

// LIKE 模式转义：\ % _ 全部转义，配 ESCAPE '\' 使用
std::string like_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() * 2);
  for (const char c : s) {
    if (c == '\\' || c == '%' || c == '_') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace

DocumentRepository::DocumentRepository(Database& db) : db_(db) {}

Result<std::vector<Document>> DocumentRepository::list(
    const DocumentFilter& filter) const {
  // 单谱单库（0007）：document 已无 lineage_id 列，也不能用 ?1 占位。
  // 首条条件直接用 1=1 保持后续占位符编号不变（?2=type / ?3=cursor / ?4=limit）。
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM document"
                          " WHERE 1=1"
                          "   AND (?2 IS NULL OR type = ?2)"
                          "   AND (?3 IS NULL OR id > ?3)"
                          " ORDER BY id LIMIT ?4";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (filter.type) {
    sqlite3_bind_text(st.get(), 2, to_string(*filter.type).c_str(), -1,
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st.get(), 2);
  }
  bind_opt_text(st.get(), 3, filter.cursor);
  sqlite3_bind_int(st.get(), 4, filter.limit > 0 ? filter.limit : 50);

  std::vector<Document> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_document(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<std::vector<Document>> DocumentRepository::search(
    std::string_view keyword, int limit, const std::string& after_id) const {
  if (keyword.empty()) return invalid_argument("检索关键词不能为空");
  const int cap = limit > 0 ? limit : 50;

  std::string sql;
  if (codepoint_count(keyword) >= 3) {
    // trigram FTS：中文子串命中。quote_fts 包成短语查询，
    // 避免关键词里的 FTS5 语法字符被当查询表达式。列名必须限定
    // document.——external-content FTS 镜像了同名列，裸列名歧义
    // （person_fts 的 search 同款处理，见 sqlite_util 的 qualify_cols）。
    // ORDER BY 与 keyset 划分键同源（同 person_fts 的跨页重复教训）
    sql = std::string("SELECT ").append(qualify_cols(kCols, "document")) +
          " FROM document JOIN document_fts"
          " ON document_fts.rowid = document.rowid"
          " WHERE document_fts MATCH ?1";
    if (!after_id.empty()) sql += " AND document.id > ?3";
    sql += " ORDER BY document.id LIMIT ?2";
  } else {
    // 1-2 字查询（「家训」「凡例」）：trigram 需 ≥3 字符，回退 LIKE
    sql = std::string("SELECT ").append(kCols) +
          " FROM document"
          " WHERE (title LIKE '%' || ?1 || '%' ESCAPE '\\'"
          "    OR content LIKE '%' || ?1 || '%' ESCAPE '\\')";
    if (!after_id.empty()) sql += " AND id > ?3";
    sql += " ORDER BY id LIMIT ?2";
  }
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备检索失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (codepoint_count(keyword) >= 3) {
    const std::string quoted = quote_fts(keyword);
    sqlite3_bind_text(st.get(), 1, quoted.c_str(), -1, SQLITE_TRANSIENT);
  } else {
    const std::string escaped = like_escape(keyword);
    sqlite3_bind_text(st.get(), 1, escaped.c_str(), -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_int(st.get(), 2, cap);
  if (!after_id.empty()) {
    sqlite3_bind_text(st.get(), 3, after_id.c_str(), -1, SQLITE_TRANSIENT);
  }

  std::vector<Document> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_document(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("检索执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<Document> DocumentRepository::get(const std::string& id) const {
  const std::string sql = std::string("SELECT ").append(kCols) +
                          " FROM document WHERE id = ?1";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(st.get());
  if (rc == SQLITE_ROW) return row_to_document(st.get());
  if (rc != SQLITE_DONE) {
    return internal(std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw()));
  }
  return not_found("文档不存在: " + id);
}

Result<Document> DocumentRepository::insert(Document doc) {
  if (doc.id.empty()) doc.id = generate_ulid();

  const std::string sql =
      "INSERT INTO document"
      " (id, type, title, author, date_text, edition, content, seq)"
      " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 INSERT 失败");
  bind_document(st.get(), doc);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("插入失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return doc;
}

Result<Document> DocumentRepository::update(const Document& d) {
  // 无 rev（篇章正文）：直接按 id 覆盖，changes()==0 即不存在。
  // 占位序与 bind_document 不同（id 在末尾），不能复用——M7.3 在
  // citation 上踩过参数序错位的坑（PATCH 必 500），update 一律
  // 手工按列对齐绑定。
  const std::string sql =
      "UPDATE document SET"
      " type=?1, title=?2, author=?3, date_text=?4,"
      " edition=?5, content=?6, seq=?7"
      " WHERE id=?8";
  Stmt st(db_.raw(), sql.c_str());
  if (!st.ok()) return internal("准备 UPDATE 失败");
  int i = 1;
  sqlite3_bind_text(st.get(), i++, to_string(d.type).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), i++, d.title.c_str(), -1, SQLITE_TRANSIENT);
  bind_opt_text(st.get(), i++, d.author);
  bind_opt_text(st.get(), i++, d.date_text);
  bind_opt_text(st.get(), i++, d.edition);
  bind_opt_text(st.get(), i++, d.content);
  if (d.seq) {
    sqlite3_bind_int64(st.get(), i++, *d.seq);
  } else {
    sqlite3_bind_null(st.get(), i++);
  }
  sqlite3_bind_text(st.get(), i++, d.id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("更新失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("文档不存在: " + d.id);
  }
  return d;
}

Result<void> DocumentRepository::remove(const std::string& id) {
  Stmt st(db_.raw(), "DELETE FROM document WHERE id = ?1");
  if (!st.ok()) return internal("准备 DELETE 失败");
  sqlite3_bind_text(st.get(), 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st.get()) != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("删除失败: ") + sqlite3_errmsg(db_.raw())};
  }
  if (sqlite3_changes(db_.raw()) == 0) {
    return not_found("文档不存在: " + id);
  }
  return {};
}

}  // namespace genealogy
