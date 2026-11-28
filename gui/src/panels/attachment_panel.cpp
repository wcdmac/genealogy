// 附件面板渲染层。

#include <genealogy/gui/panels/attachment_panel.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

namespace genealogy::gui {

namespace {
const char* kKinds[] = {"遗像", "谱页扫描", "照片", "其他"};
AttachmentKind kind_of(int idx) {
  switch (idx) {
    case 0: return AttachmentKind::Portrait;
    case 1: return AttachmentKind::DocumentScan;
    case 2: return AttachmentKind::Photo;
    default: return AttachmentKind::Other;
  }
}
}  // namespace

void AttachmentPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    return;
  }
  // 每库装载一次：空库时 rows_ 恒空，旧行为每帧 60fps 连环查询（第四轮 P2）
  if (!loaded_ || loaded_db_ != state_.db_path) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("附件读取失败: " + why, true);
    loaded_ = true;
    loaded_db_ = state_.db_path;
  }

  ImGui::SeparatorText("导入文件（按 sha256 去重）");
  ImGui::SetNextItemWidth(360);
  input_text("文件路径", &path_buf_);
  ImGui::SetNextItemWidth(120);
  ImGui::Combo("类型", &kind_idx_, kKinds, 4);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  input_text("关联人物 id", &subject_buf_);
  if (subject_buf_.empty() && !state_.selected_person_id.empty())
    subject_buf_ = state_.selected_person_id;
  ImGui::SetNextItemWidth(300);
  input_text("说明", &caption_buf_);
  if (ImGui::Button("导入", ImVec2(120, 0))) {
    std::string why;
    if (!import_file(session, path_buf_, kind_of(kind_idx_), subject_buf_,
                     caption_buf_, &why))
      state_.set_status("导入失败: " + why, true);
    else
      state_.set_status("附件已导入");
  }
  ImGui::SameLine();
  if (ImGui::Button("按人物筛选")) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("附件读取失败: " + why, true);
  }
  ImGui::SameLine();
  if (ImGui::Button("显示全部")) {
    subject_buf_.clear();
    std::string why;
    reload(session, &why);
  }

  ImGui::SeparatorText(("附件（共 " + std::to_string(rows_.size()) + " 项）").c_str());
  ImGui::BeginChild("##attachments", ImVec2(0, 300), true);
  for (const auto& a : rows_) {
    ImGui::PushID(a.id.c_str());
    ImGui::Text("%s  %s  %lld B", a.filename.c_str(),
                a.caption ? a.caption->c_str() : "",
                static_cast<long long>(a.size_bytes));
    ImGui::SameLine();
    if (ImGui::SmallButton("打开")) {
      const auto p = session.attachments().content_path(a.sha256);
      if (!p.empty()) state_.set_status("附件路径: " + p.string());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("删除")) {
      std::string why;
      if (!remove(session, a.id, &why))
        state_.set_status("删除失败: " + why, true);
    }
    ImGui::TextDisabled("  sha256=%s  主体=%s/%s", a.sha256.c_str(),
                        a.subject_type.c_str(), a.subject_id.c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();
}

}  // namespace genealogy::gui
