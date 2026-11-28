// 校验面板的渲染层（ImGui）。装载/校验/采纳在 validation_panel_state.cpp
// ——无 imgui 依赖、可被 gui-tests 无头驱动；本 TU 只把状态机的 API
// 画出来，不含任何装配逻辑。

#include <genealogy/gui/panels/validation_panel.hpp>

#include <imgui.h>

namespace genealogy::gui {

void ValidationPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }

  // 采纳后台结果 + 按需启动：状态机在 validation_panel_state.cpp
  // （可无头单测），这里只画。
  poll();

  if (ImGui::Button("运行校验")) {
    start_run(session);
  }
  ImGui::SameLine();
  if (run_in_flight()) {
    ImGui::TextDisabled("校验中…（后台线程跑，UI 不冻结）");
    return;
  }
  ImGui::Text("规则 %d/%d", rules_run_, rules_total_);
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "error %d", error_count_);
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.2F, 1.0F), "warning %d",
                     warning_count_);
  ImGui::SameLine();
  ImGui::TextDisabled("info %d", info_count_);

  ImGui::Separator();

  if (!ran_) {
    ImGui::TextDisabled("点「运行校验」跑一遍全库一致性规则。");
    return;
  }
  if (issues_.empty()) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1.0F), "未发现问题");
    return;
  }

  if (ImGui::BeginTable("issues", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("等级", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("规则", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("实体", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("说明");
    ImGui::TableSetupColumn("定位", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    int issue_row = 0;
    for (const auto& i : issues_) {
      ImGui::TableNextRow();
      const ImVec4 color = i.severity == "error"
                               ? ImVec4(1.0F, 0.4F, 0.4F, 1.0F)
                               : i.severity == "warning"
                                     ? ImVec4(1.0F, 0.85F, 0.2F, 1.0F)
                                     : ImVec4(0.6F, 0.6F, 0.6F, 1.0F);
      ImGui::TableNextColumn();
      ImGui::TextColored(color, "%s", i.severity.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(i.rule_id.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(i.entity_type.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(i.message.c_str());
      ImGui::TableNextColumn();
      // 点击定位：人物问题跳到人物卡（entity_type == "person" 才有意义）。
      // 同一人多条问题时 label 哈希会撞 ID，补行号区分（第四轮 P2）。
      if (i.entity_type == "person" &&
          ImGui::SmallButton(
              ("查看##" + i.entity_id + "#" + std::to_string(issue_row))
                  .c_str())) {
        state_.selected_person_id = i.entity_id;
      }
      ++issue_row;
    }
    ImGui::EndTable();
  }
}

}  // namespace genealogy::gui