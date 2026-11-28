#pragma once

// 附件用例层：把文件登记进附件表（文件内容另存到附件目录，按 sha256 命名）。
// ADR：GUI 只经用例层访问数据；附件内容存在库外的 attachments/ 子目录，
// 与 db backup/restore 的打包口径一致。

#include <genealogy/error.hpp>
#include <genealogy/model/attachment.hpp>
#include <genealogy/repository/database.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace genealogy {

struct AttachmentFilter {
  std::optional<std::string> subject_type;
  std::optional<std::string> subject_id;
  std::optional<AttachmentKind> kind;
  int limit = 200;
};

class AttachmentService {
 public:
  explicit AttachmentService(Database& db);

  // 登记已有文件：读内容算 sha256，把文件复制到 attachments/<sha256>（若
  // 同名已存在则视为去重，直接复用）。返回登记后的元数据。
  Result<Attachment> import_file(const std::filesystem::path& source,
                                 AttachmentKind kind,
                                 const std::string& subject_type,
                                 const std::string& subject_id,
                                 const std::string& caption);

  Result<std::vector<Attachment>> list(const AttachmentFilter& filter) const;
  Result<Attachment> get(const std::string& id) const;
  Result<void> remove(const std::string& id);

  // 级联删除某主体的全部附件（行 + 不再被引用的内容文件）。person 删除
  // 路径专用：subject_type=="person" 时先清掉本人的头像引用（否则被自己的
  // 头像引用挡住）；其他人物引用这些附件作头像时仍以 409 拒绝（FK 会拦，
  // 这里给出人话）。返回删除的行数。
  Result<long> remove_subject_attachments(const std::string& subject_type,
                                          const std::string& subject_id);

  // 附件内容实际路径（不存在则空）
  std::filesystem::path content_path(const std::string& sha256) const;

 private:
  Database* db_;
  std::filesystem::path attachments_dir() const;
};

}  // namespace genealogy
