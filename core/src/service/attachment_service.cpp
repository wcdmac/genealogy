#include <genealogy/service/attachment_service.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/util/sha256.hpp>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "../repository/sqlite_util.hpp"

namespace genealogy {

namespace fs = std::filesystem;
using repo_internal::col_opt_text;
using repo_internal::col_text;

AttachmentService::AttachmentService(Database& db) : db_(&db) {}

fs::path AttachmentService::attachments_dir() const {
  // Database 的 path 未暴露；从 PRAGMA database_list 取（单库形态）。
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), "PRAGMA database_list", -1, &st, nullptr) ==
      SQLITE_OK) {
    if (sqlite3_step(st) == SQLITE_ROW) {
      const auto* file = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
      if (file != nullptr && *file != '\0') {
        const fs::path p(file);
        sqlite3_finalize(st);
        return p.parent_path() / "attachments";
      }
    }
    sqlite3_finalize(st);
  }
  return fs::path("attachments");
}

fs::path AttachmentService::content_path(const std::string& sha) const {
  if (sha.empty()) return {};
  return attachments_dir() / sha;
}

Result<Attachment> AttachmentService::import_file(
    const fs::path& source, AttachmentKind kind,
    const std::string& subject_type, const std::string& subject_id,
    const std::string& caption) {
  std::error_code ec;
  if (!fs::is_regular_file(source, ec))
    return Error{ErrorCode::NotFound, "附件文件不存在: " + source.string()};
  if (subject_id.empty())
    return Error{ErrorCode::InvalidArgument, "附件主体 id 不能为空"};

  const std::string sha = sha256_file(source.string());
  if (sha.empty())
    return Error{ErrorCode::IoError, "读取附件失败: " + source.string()};

  const fs::path dir = attachments_dir();
  fs::create_directories(dir, ec);
  if (ec)
    return Error{ErrorCode::IoError, "建附件目录失败: " + ec.message()};
  const fs::path dest = dir / sha;
  if (!fs::exists(dest, ec)) {
    fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
    if (ec)
      return Error{ErrorCode::IoError, "复制附件失败: " + ec.message()};
  }

  Attachment a;
  a.id = generate_ulid();
  a.kind = kind;
  a.filename = source.filename().string();
  a.mime = "application/octet-stream";
  a.sha256 = sha;
  a.size_bytes = static_cast<std::int64_t>(fs::file_size(source, ec));
  a.subject_type = subject_type;
  a.subject_id = subject_id;
  if (!caption.empty()) a.caption = caption;

  const char* sql =
      "INSERT INTO attachment(id,kind,filename,mime,sha256,size_bytes,"
      "subject_type,subject_id,caption) VALUES(?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), sql, -1, &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::IoError, "附件登记语句准备失败"};
  sqlite3_bind_text(st, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
  const std::string kind_str = to_string(a.kind);
  sqlite3_bind_text(st, 2, kind_str.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, a.filename.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, a.mime.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, a.sha256.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 6, static_cast<sqlite3_int64>(a.size_bytes));
  sqlite3_bind_text(st, 7, a.subject_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 8, a.subject_id.c_str(), -1, SQLITE_TRANSIENT);
  if (a.caption) sqlite3_bind_text(st, 9, a.caption->c_str(), -1, SQLITE_TRANSIENT);
  else sqlite3_bind_null(st, 9);
  const bool done = sqlite3_step(st) == SQLITE_DONE;
  const std::string err = done ? "" : sqlite3_errmsg(db_->raw());
  sqlite3_finalize(st);
  if (!done) return Error{ErrorCode::IoError, "附件登记失败: " + err};
  return a;
}

Result<std::vector<Attachment>> AttachmentService::list(
    const AttachmentFilter& filter) const {
  std::string sql =
      "SELECT id,kind,filename,mime,sha256,size_bytes,subject_type,subject_id,"
      "caption FROM attachment WHERE 1=1";
  if (filter.subject_type) sql += " AND subject_type=?";
  if (filter.subject_id) sql += " AND subject_id=?";
  if (filter.kind) sql += " AND kind=?";
  sql += " ORDER BY filename LIMIT ?";

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::IoError, "附件查询准备失败"};
  int idx = 1;
  if (filter.subject_type) sqlite3_bind_text(st, idx++, filter.subject_type->c_str(), -1, SQLITE_TRANSIENT);
  if (filter.subject_id) sqlite3_bind_text(st, idx++, filter.subject_id->c_str(), -1, SQLITE_TRANSIENT);
  if (filter.kind) {
    const std::string k = to_string(*filter.kind);
    sqlite3_bind_text(st, idx++, k.c_str(), -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_int(st, idx++, filter.limit);

  std::vector<Attachment> out;
  while (sqlite3_step(st) == SQLITE_ROW) {
    Attachment a;
    a.id = col_text(st, 0);
    a.kind = attachment_kind_from_string(col_text(st, 1)).value_or(AttachmentKind::Other);
    a.filename = col_text(st, 2);
    a.mime = col_text(st, 3);
    a.sha256 = col_text(st, 4);
    a.size_bytes = sqlite3_column_int64(st, 5);
    a.subject_type = col_text(st, 6);
    a.subject_id = col_text(st, 7);
    a.caption = col_opt_text(st, 8);
    out.push_back(std::move(a));
  }
  sqlite3_finalize(st);
  return out;
}

Result<Attachment> AttachmentService::get(const std::string& id) const {
  const char* sql =
      "SELECT id,kind,filename,mime,sha256,size_bytes,subject_type,subject_id,"
      "caption FROM attachment WHERE id=?";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), sql, -1, &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::IoError, "附件查询准备失败"};
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    return Error{ErrorCode::NotFound, "附件不存在: " + id};
  }
  Attachment a;
  a.id = col_text(st, 0);
  a.kind = attachment_kind_from_string(col_text(st, 1)).value_or(AttachmentKind::Other);
  a.filename = col_text(st, 2);
  a.mime = col_text(st, 3);
  a.sha256 = col_text(st, 4);
  a.size_bytes = sqlite3_column_int64(st, 5);
  a.subject_type = col_text(st, 6);
  a.subject_id = col_text(st, 7);
  a.caption = col_opt_text(st, 8);
  sqlite3_finalize(st);
  return a;
}

Result<void> AttachmentService::remove(const std::string& id) {
  auto a = get(id);
  if (!a) return a.error();
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), "DELETE FROM attachment WHERE id=?", -1,
                         &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::IoError, "删除附件语句准备失败"};
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const bool done = sqlite3_step(st) == SQLITE_DONE;
  const std::string err = done ? "" : sqlite3_errmsg(db_->raw());
  sqlite3_finalize(st);
  if (!done) return Error{ErrorCode::IoError, "删除附件失败: " + err};
  return {};
}

}  // namespace genealogy
