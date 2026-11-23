// 地点管理面板的渲染层。

#include <genealogy/gui/panels/place_panel.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

namespace genealogy::gui {

void PlacePanel::draw(DbSession& session) {
  if (!ImGui::Begin(title(), &open)) { ImGui::End(); return; }
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    ImGui::End();
    return;
  }
  if (rows_.empty()) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("地点读取失败: " + why, true);
  }

  ImGui::BeginChild("##places", ImVec2(0, 180), true);
  for (const auto& p : rows_) {
    ImGui::PushID(p.id.c_str());
    if (ImGui::Selectable(p.name.c_str(), p.id == draft_.id)) select(p);
    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::SeparatorText(editing() ? "编辑地点" : "新建地点");
  ImGui::SetNextItemWidth(180);
  input_text("名称", &name_buf_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  input_text("今属地", &modern_buf_);
  ImGui::SetNextItemWidth(180);
  input_text("历史名/沿革", &hist_buf_);
  ImGui::SetNextItemWidth(100);
  input_text("纬度", &lat_buf_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  input_text("经度", &lng_buf_);
  input_text_multiline("##place_note", &note_buf_, ImGui::GetTextLineHeight() * 2);

  if (ImGui::Button(editing() ? "保存修改" : "新建", ImVec2(120, 0))) {
    sync_draft();
    std::string why;
    if (!save(session, &why)) state_.set_status("地点保存失败: " + why, true);
    else state_.set_status("地点已保存");
  }
  ImGui::SameLine();
  if (ImGui::Button("新建空白")) new_draft();
  ImGui::SameLine();
  if (ImGui::Button("刷新")) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("地点读取失败: " + why, true);
  }
  ImGui::TextDisabled("共 %zu 个地点", rows_.size());
  ImGui::End();
}

}  // namespace genealogy::gui
