// 字辈派语面板的渲染层。

#include <genealogy/gui/panels/poem_panel.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

namespace genealogy::gui {

void PoemPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    return;
  }
  bool branch = branch_scope_;
  if (ImGui::RadioButton("谱级派语", !branch)) {
    branch = false; set_scope_branch(false, "book");
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("支派派语", branch)) {
    branch = true;
    // 同步回成员，否则下一帧被 branch_scope_ 覆回 false，
    // reload/set/check 仍按谱级 book 执行——支派派语实际不可用。
    set_scope_branch(true, branch_id_buf_.empty() ? "book" : branch_id_buf_);
  }
  if (branch) {
    ImGui::SetNextItemWidth(220);
    input_text("支派 ID", &branch_id_buf_);
    if (owner_id_buf_ != branch_id_buf_)
      set_scope_branch(true, branch_id_buf_.empty() ? "book" : branch_id_buf_);
  }
  if (ImGui::Button("读取")) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("派语读取失败: " + why, true);
    else state_.set_status("已读取派语");
  }

  ImGui::SeparatorText("联句原文（只取汉字，标点可留）");
  input_text_multiline("##poem_text", &text_buf_, ImGui::GetTextLineHeight() * 4);
  if (ImGui::Button("保存派语", ImVec2(120, 0))) {
    std::string why;
    if (!set(session, &why)) state_.set_status("派语保存失败: " + why, true);
    else state_.set_status("派语已保存");
  }
  ImGui::SameLine();
  if (ImGui::Button("一致性校验")) {
    std::string why;
    if (!check(session, &why)) state_.set_status("校验失败: " + why, true);
    else state_.set_status(conflicts_.empty() ? "派语校验通过" :
                           "派语发现 " + std::to_string(conflicts_.size()) + " 处冲突");
  }

  ImGui::SeparatorText(("辈字（共 " + std::to_string(entries_.size()) + " 字）").c_str());
  ImGui::BeginChild("##poem_entries", ImVec2(0, 140), true);
  for (const auto& e : entries_)
    ImGui::Text("第 %d 世  %s", e.seq, e.character.c_str());
  ImGui::EndChild();

  if (!conflicts_.empty()) {
    ImGui::SeparatorText("冲突");
    for (const auto& c : conflicts_)
      ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.3F, 1.0F), "%s", c.c_str());
  }
}

}  // namespace genealogy::gui
