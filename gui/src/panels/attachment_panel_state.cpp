// 附件面板状态机（无 ImGui 依赖）。

#include <genealogy/gui/panels/attachment_panel.hpp>

namespace genealogy::gui {

bool AttachmentPanel::reload(DbSession& session, std::string* why) {
  AttachmentFilter f;
  if (!subject_buf_.empty()) f.subject_id = subject_buf_;
  auto got = session.attachments().list(f);
  if (!got) { *why = got.error().message(); return false; }
  rows_ = *got;
  return true;
}

bool AttachmentPanel::import_file(DbSession& session, const std::string& path,
                                  AttachmentKind kind,
                                  const std::string& subject_id,
                                  const std::string& caption,
                                  std::string* why) {
  if (subject_id.empty()) { *why = "请填写关联人物 id"; return false; }
  auto added = session.attachments().import_file(path, kind, "person",
                                                 subject_id, caption);
  if (!added) { *why = added.error().message(); return false; }
  return reload(session, why);
}

bool AttachmentPanel::remove(DbSession& session, const std::string& id,
                             std::string* why) {
  auto r = session.attachments().remove(id);
  if (!r) { *why = r.error().message(); return false; }
  return reload(session, why);
}

void AttachmentPanel::clear() {
  loaded_db_.clear();
  loaded_ = false;
  rows_.clear();
}

}  // namespace genealogy::gui
