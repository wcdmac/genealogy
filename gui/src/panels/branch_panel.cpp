// 支派管理面板的渲染层。

#include <genealogy/gui/panels/branch_panel.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

namespace genealogy::gui {

void BranchPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    return;
  }
  if (rows_.empty()) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("支派读取失败: " + why, true);
  }

  ImGui::Columns(3, "##branches", false);
  ImGui::TextUnformatted("支派名"); ImGui::NextColumn();
  ImGui::TextUnformatted("始迁祖/父房"); ImGui::NextColumn();
  ImGui::TextUnformatted("备注"); ImGui::NextColumn();
  ImGui::Separator();
  for (const auto& b : rows_) {
    ImGui::PushID(b.id.c_str());
    if (ImGui::Selectable(b.name.c_str(), b.id == draft_.id)) select(b);
    ImGui::NextColumn();
    ImGui::TextDisabled("%s", b.parent_branch_id ? b.parent_branch_id->c_str() : "—");
    ImGui::NextColumn();
    ImGui::TextDisabled("%s", b.note ? b.note->c_str() : "");
    ImGui::NextColumn();
    ImGui::PopID();
  }
  ImGui::Columns(1);

  ImGui::SeparatorText(editing() ? "编辑支派" : "新建支派");
  ImGui::SetNextItemWidth(180);
  input_text("名称", &name_buf_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  input_text("世代字", &generation_buf_);
  input_text_multiline("##branch_note", &note_buf_, ImGui::GetTextLineHeight() * 3);

  if (ImGui::Button(editing() ? "保存修改" : "新建", ImVec2(120, 0))) {
    sync_draft();
    std::string why;
    if (!save(session, &why)) state_.set_status("支派保存失败: " + why, true);
    else state_.set_status("支派已保存");
  }
  ImGui::SameLine();
  if (ImGui::Button("新建空白")) new_draft();
  ImGui::SameLine();
  if (ImGui::Button("刷新")) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("支派读取失败: " + why, true);
  }
  ImGui::TextDisabled("共 %zu 个支派", rows_.size());
}

}  // namespace genealogy::gui
