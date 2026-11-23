#include <genealogy/model/attachment.hpp>

namespace genealogy {

std::string to_string(AttachmentKind kind) {
  switch (kind) {
    case AttachmentKind::Portrait: return "portrait";
    case AttachmentKind::DocumentScan: return "document_scan";
    case AttachmentKind::Photo: return "photo";
    case AttachmentKind::Other: break;
  }
  return "other";
}

std::optional<AttachmentKind> attachment_kind_from_string(const std::string& s) {
  if (s == "portrait") return AttachmentKind::Portrait;
  if (s == "document_scan") return AttachmentKind::DocumentScan;
  if (s == "photo") return AttachmentKind::Photo;
  if (s == "other") return AttachmentKind::Other;
  return std::nullopt;
}

nlohmann::json to_json(const Attachment& v) {
  nlohmann::json j;
  j["id"] = v.id;
  j["kind"] = to_string(v.kind);
  j["filename"] = v.filename;
  j["mime"] = v.mime;
  j["sha256"] = v.sha256;
  j["size_bytes"] = v.size_bytes;
  j["subject_type"] = v.subject_type;
  j["subject_id"] = v.subject_id;
  if (v.caption) j["caption"] = *v.caption;
  return j;
}

}  // namespace genealogy
