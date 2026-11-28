#include <genealogy/service/attachment_service.hpp>

#include <genealogy/repository/transaction.hpp>
#include <genealogy/ulid.hpp>
#include <genealogy/util/sha256.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

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

// 常见格式的粗 MIME 映射；未识别扩展名一律 octet-stream（渲染端按
// magic bytes 兜底判别,这里只影响导出描述）。
std::string mime_for(const std::string& filename) {
  const auto dot = filename.rfind('.');
  if (dot == std::string::npos) return "application/octet-stream";
  const std::string ext = filename.substr(dot + 1);
  // ASCII 大小写不敏感比较（第五轮 CI 修）：_stricmp 是 MSVC 专属，
  // Linux/macOS 的 GCC/Clang 没有这个名字（stricmp/strcasecmp 各成一套），
  // 自写等值比较保持全平台可移植。
  auto ieq = [&](const char* e) {
    if (ext.size() != std::strlen(e)) return false;
    for (std::size_t i = 0; i < ext.size(); ++i) {
      char a = ext[i], b = e[i];
      if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
      if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
      if (a != b) return false;
    }
    return true;
  };
  if (ieq("jpg") || ieq("jpeg")) return "image/jpeg";
  if (ieq("png")) return "image/png";
  if (ieq("gif")) return "image/gif";
  if (ieq("webp")) return "image/webp";
  if (ieq("bmp")) return "image/bmp";
  if (ieq("pdf")) return "application/pdf";
  if (ieq("txt")) return "text/plain";
  if (ieq("html") || ieq("htm")) return "text/html";
  if (ieq("svg")) return "image/svg+xml";
  return "application/octet-stream";
}

fs::path AttachmentService::content_path(const std::string& sha) const {
  // sha 必须是 64 位小写十六进制（sha256_hex 的输出）；否则拒绝拼接
  // 路径，防将来外部传入 "../../etc/passwd" 形成目录穿越。
  if (sha.size() != 64 ||
      !std::all_of(sha.begin(), sha.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      })) {
    return {};
  }
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

  Attachment a;
  a.id = generate_ulid();
  a.kind = kind;
  a.filename = source.filename().string();
  a.mime = mime_for(source.filename().string());
  a.sha256 = sha;
  const auto size = fs::file_size(source, ec);
  if (ec)
    return Error{ErrorCode::IoError, "读取附件大小失败: " + ec.message()};
  a.size_bytes = static_cast<std::int64_t>(size);
  a.subject_type = subject_type;
  a.subject_id = subject_id;
  if (!caption.empty()) a.caption = caption;

  const char* sql =
      "INSERT INTO attachment(id,kind,filename,mime,sha256,size_bytes,"
      "subject_type,subject_id,caption) VALUES(?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), sql, -1, &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::Internal, "附件登记语句准备失败"};
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
  if (!done) return Error{ErrorCode::Internal, "附件登记失败: " + err};

  // 行登记成功后才落内容文件（第四轮 P3）：旧顺序在行插入失败时留下
  // 永不回收的孤儿文件。内容寻址 + 先查后写，重复导入天然幂等；万一
  // 复制失败回滚行删除，不留半截登记。
  if (!fs::exists(dest, ec)) {
    fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      sqlite3_stmt* st2 = nullptr;
      if (sqlite3_prepare_v2(db_->raw(), "DELETE FROM attachment WHERE id=?",
                             -1, &st2, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st2, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st2);
        sqlite3_finalize(st2);
      }
      return Error{ErrorCode::IoError, "复制附件失败: " + ec.message()};
    }
  }
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
    return Error{ErrorCode::Internal, "附件查询准备失败"};
  int idx = 1;
  if (filter.subject_type) sqlite3_bind_text(st, idx++, filter.subject_type->c_str(), -1, SQLITE_TRANSIENT);
  if (filter.subject_id) sqlite3_bind_text(st, idx++, filter.subject_id->c_str(), -1, SQLITE_TRANSIENT);
  if (filter.kind) {
    const std::string k = to_string(*filter.kind);
    sqlite3_bind_text(st, idx++, k.c_str(), -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_int(st, idx++, filter.limit > 0 ? filter.limit : 200);

  std::vector<Attachment> out;
  int step_rc = SQLITE_OK;
  while ((step_rc = sqlite3_step(st)) == SQLITE_ROW) {
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
  if (step_rc != SQLITE_DONE)
    return Error{ErrorCode::Internal,
                 "附件查询失败: " + std::string(sqlite3_errmsg(db_->raw()))};
  return out;
}

Result<Attachment> AttachmentService::get(const std::string& id) const {
  const char* sql =
      "SELECT id,kind,filename,mime,sha256,size_bytes,subject_type,subject_id,"
      "caption FROM attachment WHERE id=?";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), sql, -1, &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::Internal, "附件查询准备失败"};
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const int step_rc = sqlite3_step(st);
  if (step_rc != SQLITE_ROW) {
    const std::string why = sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (step_rc == SQLITE_DONE)
      return Error{ErrorCode::NotFound, "附件不存在: " + id};
    return Error{ErrorCode::Internal, "附件查询失败: " + why};
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

Result<std::vector<std::string>> AttachmentService::remove_subject_attachments(
    const std::string& subject_type, const std::string& subject_id) {
  // person 删除级联：先清本人的头像引用（本人即将删除，链接必然消失；
  // 其他人物对这些附件的引用仍被下面的预检拦截），再逐行删除并回收文件。
  // 全程在调用方事务内（嵌套感知），失败由外层回滚。
  if (subject_type == "person") {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "UPDATE person SET portrait_attachment_id=NULL"
                           " WHERE id=?1 AND portrait_attachment_id IN"
                           " (SELECT id FROM attachment"
                           "  WHERE subject_type=?2 AND subject_id=?1)",
                           -1, &st, nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "头像引用清理准备失败"};
    sqlite3_bind_text(st, 1, subject_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, subject_type.c_str(),
                      static_cast<int>(subject_type.size()),
                      SQLITE_TRANSIENT);
    const bool done = sqlite3_step(st) == SQLITE_DONE;
    const std::string err = done ? "" : sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (!done) return Error{ErrorCode::Internal, "头像引用清理失败: " + err};
  }
  // 其他人物还在把这些附件用作头像 → 409，先解除引用再删人
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "SELECT COUNT(*) FROM person"
                           " WHERE portrait_attachment_id IN"
                           " (SELECT id FROM attachment"
                           "  WHERE subject_type=?1 AND subject_id=?2)"
                           " AND id<>?2",
                           -1, &st, nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "头像引用查询准备失败"};
    sqlite3_bind_text(st, 1, subject_type.c_str(),
                      static_cast<int>(subject_type.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, subject_id.c_str(), -1, SQLITE_TRANSIENT);
    const bool row = sqlite3_step(st) == SQLITE_ROW;
    const long n = row ? sqlite3_column_int64(st, 0) : -1;
    const std::string err = row ? "" : sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (!row) return Error{ErrorCode::Internal, "头像引用查询失败: " + err};
    if (n > 0) {
      return conflict("该人物的附件被其他 " + std::to_string(n) +
                      " 位人物用作头像，先解除引用再删除");
    }
  }
  // 取待删行（行数 + sha 集合）
  std::vector<std::pair<std::string, std::string>> rows;  // (id, sha)
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "SELECT id, sha256 FROM attachment"
                           " WHERE subject_type=?1 AND subject_id=?2",
                           -1, &st, nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "附件枚举准备失败"};
    sqlite3_bind_text(st, 1, subject_type.c_str(),
                      static_cast<int>(subject_type.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, subject_id.c_str(), -1, SQLITE_TRANSIENT);
    for (;;) {
      const int rc = sqlite3_step(st);
      if (rc == SQLITE_ROW) {
        rows.emplace_back(col_text(st, 0), col_text(st, 1));
      } else if (rc == SQLITE_DONE) {
        break;
      } else {
        const std::string err = sqlite3_errmsg(db_->raw());
        sqlite3_finalize(st);
        return Error{ErrorCode::Internal, "附件枚举失败: " + err};
      }
    }
    sqlite3_finalize(st);
  }
  for (const auto& [att_id, sha] : rows) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "DELETE FROM attachment WHERE id=?", -1, &st,
                           nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "删除附件语句准备失败"};
    sqlite3_bind_text(st, 1, att_id.c_str(), static_cast<int>(att_id.size()),
                      SQLITE_TRANSIENT);
    const bool done = sqlite3_step(st) == SQLITE_DONE;
    const std::string err = done ? "" : sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (!done) return Error{ErrorCode::Internal, "删除附件失败: " + err};
  }
  // 待回收 sha 清单（去重）：调用方事务提交后经 reclaim_content_files
  // 删除——文件删除不随事务回滚，不能发生在提交之前。
  std::vector<std::string> reclaim;
  for (const auto& [att_id, sha] : rows) {
    if (std::find(reclaim.begin(), reclaim.end(), sha) == reclaim.end()) {
      reclaim.push_back(sha);
    }
  }
  return reclaim;
}

void AttachmentService::reclaim_content_files(
    const std::vector<std::string>& shas) const {
  for (const auto& sha : shas) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "SELECT COUNT(*) FROM attachment WHERE sha256=?1",
                           -1, &st, nullptr) != SQLITE_OK) {
      continue;
    }
    sqlite3_bind_text(st, 1, sha.c_str(), static_cast<int>(sha.size()),
                      SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW &&
        sqlite3_column_int64(st, 0) == 0) {
      std::error_code ec;
      std::filesystem::remove(content_path(sha), ec);
    }
    sqlite3_finalize(st);
  }
}

Result<void> AttachmentService::remove(const std::string& id) {
  auto a = get(id);
  if (!a) return a.error();
  // 预检与删除同一写事务（与其余删除保护同口径）；人物头像引用该附件时
  // 以 409 拒绝，而不是让 FK 以 500 冒出（第四轮 P2-3）。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "SELECT COUNT(*) FROM person"
                           " WHERE portrait_attachment_id=?1",
                           -1, &st, nullptr) != SQLITE_OK)
      return Error{ErrorCode::IoError, "头像引用查询准备失败"};
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool row = sqlite3_step(st) == SQLITE_ROW;
    const long n = row ? sqlite3_column_int64(st, 0) : -1;
    const std::string qerr = row ? "" : sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    if (!row) return Error{ErrorCode::Internal, "头像引用查询失败: " + qerr};
    if (n > 0) {
      return conflict("附件被 " + std::to_string(n) +
                      " 位人物用作头像，先解除引用再删除");
    }
  }
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(), "DELETE FROM attachment WHERE id=?", -1,
                         &st, nullptr) != SQLITE_OK)
    return Error{ErrorCode::IoError, "删除附件语句准备失败"};
  sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const bool done = sqlite3_step(st) == SQLITE_DONE;
  const std::string err = done ? "" : sqlite3_errmsg(db_->raw());
  sqlite3_finalize(st);
  if (!done) return Error{ErrorCode::Internal, "删除附件失败: " + err};
  if (auto c = tx.commit(); !c) return c.error();
  // 内容寻址文件回收：同一 sha 可能被多条附件行共享，仅当不再被引用时删。
  // 文件删除失败（占用/权限）不影响已提交的行删除，留待再次清理。
  {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_->raw(),
                           "SELECT COUNT(*) FROM attachment WHERE sha256=?1",
                           -1, &st, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, a->sha256.c_str(),
                        static_cast<int>(a->sha256.size()), SQLITE_TRANSIENT);
      if (sqlite3_step(st) == SQLITE_ROW &&
          sqlite3_column_int64(st, 0) == 0) {
        std::error_code ec;
        std::filesystem::remove(content_path(a->sha256), ec);
      }
      sqlite3_finalize(st);
    }
  }
  return {};
}

}  // namespace genealogy
