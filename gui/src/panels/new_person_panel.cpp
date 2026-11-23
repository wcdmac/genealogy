// 新建人物面板的渲染层（ImGui）。所有数据装配在 *_state.cpp。

#include <genealogy/gui/panels/new_person_panel.hpp>

#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

#include <string>

namespace genealogy::gui {

void NewPersonPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    return;
  }

  // 焦点变化时重预填（智能预填，用户改动不被覆盖）
  if (!state_.selected_person_id.empty() &&
      state_.selected_person_id != prefill_from_id_)
    prefill(session, state_.selected_person_id);

  if (ImGui::Button("清空")) clear();
  ImGui::SameLine();
  ImGui::TextDisabled("以当前焦点人物预填姓氏/世代");

  ImGui::SeparatorText("基本信息");
  ImGui::SetNextItemWidth(120);
  input_text("姓", &draft_.family_name);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  input_text("名*", &draft_.given_name);
  ImGui::SetNextItemWidth(120);
  input_text("字", &draft_.courtesy_name);
  ImGui::SameLine();
  ImGui::TextUnformatted("性别");
  ImGui::RadioButton("男##np", &draft_.gender, 0);
  ImGui::SameLine();
  ImGui::RadioButton("女##np", &draft_.gender, 1);
  ImGui::SameLine();
  ImGui::RadioButton("未知##np", &draft_.gender, 2);

  ImGui::SetNextItemWidth(160);
  input_text("出生（ISO 或老谱原文）", &draft_.birth_date);
  ImGui::SetNextItemWidth(160);
  input_text("卒期（ISO 或老谱原文）", &draft_.death_date);
  ImGui::SetNextItemWidth(80);
  input_text("世代", &draft_.generation_no);
  ImGui::SameLine();
  ImGui::TextDisabled("老谱原文如「同治三年四月初四」自动认年号");

  ImGui::SeparatorText("关系（可选）");
  ImGui::SetNextItemWidth(220);
  input_text("父亲 ID", &draft_.father_id);
  ImGui::SameLine();
  if (ImGui::Button("用焦点##dad") && !state_.selected_person_id.empty())
    draft_.father_id = state_.selected_person_id;
  ImGui::SetNextItemWidth(220);
  input_text("母亲 ID", &draft_.mother_id);

  ImGui::SeparatorText("行传 / 备注");
  input_text_multiline("##np_bio", &draft_.biography, ImGui::GetTextLineHeight() * 5);
  input_text_multiline("##np_note", &draft_.note, ImGui::GetTextLineHeight() * 3);

  if (ImGui::Button("创建人物", ImVec2(160, 0))) create(session);
}

}  // namespace genealogy::gui
