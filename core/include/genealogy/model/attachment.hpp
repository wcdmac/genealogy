#pragma once

// 附件（遗像/照片/扫描件/其他）。attachment 表无 rev：内容不可变（按 sha256
// 去重），改图即新增附件并换主体的引用。

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

enum class AttachmentKind { Portrait, DocumentScan, Photo, Other };

std::string to_string(AttachmentKind kind);
std::optional<AttachmentKind> attachment_kind_from_string(const std::string& s);

// subject_type 与 citation 等复用同一组字面量（person/union/branch/lineage）
struct Attachment {
  std::string id;
  AttachmentKind kind = AttachmentKind::Photo;
  std::string filename;
  std::string mime;
  std::string sha256;
  std::int64_t size_bytes = 0;
  std::string subject_type;
  std::string subject_id;
  std::optional<std::string> caption;
};

nlohmann::json to_json(const Attachment& v);

}  // namespace genealogy
