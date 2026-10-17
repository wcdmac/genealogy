#include <genealogy/gui/panels/validation_panel.hpp>

#include <imgui.h>

#include <string>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/validate/validation.hpp>

namespace genealogy::gui {

void ValidationPanel::run(DbSession& session) {
  issues_.clear();
  error_count_ = warning_count_ = info_count_ = 0;
  rules_run_ = rules_total_ = 0;

  // 建图 + 事件（与 CLI io validate 同一条装载路径）
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = session.persons().list_persons(pf);
  if (!persons) {
    state_.set_status("载入人物失败: " + persons.error().message(), true);
    ran_ = false;
    return;
  }
  UnionFilter uf;
  uf.limit = 200000;
  auto unions = session.unions().list_unions(uf);
  if (!unions) {
    state_.set_status("载入结合失败: " + unions.error().message(), true);
    ran_ = false;
    return;
  }
  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(std::move(*persons), std::move(*unions));

  EventRepository events(session.db());
  EventFilter ef;
  ef.limit = 300000;
  auto evs = events.list(ef);
  if (!evs) {
    state_.set_status("载入事件失败: " + evs.error().message(), true);
    ran_ = false;
    return;
  }

  auto report = validate(graph, &*evs, ValidationOptions{});
  if (!report) {
    state_.set_status("校验失败: " + report.error().message(), true);
    ran_ = false;
    return;
  }

  for (const auto& i : report->issues) {
    IssueRow r;
    r.rule_id = i.rule_id;
    r.severity = to_string(i.severity);
    r.entity_type = i.entity_type;
    r.entity_id = i.entity_id;
    r.message = i.message;
    r.suggestion = i.suggestion;
    issues_.push_back(std::move(r));
  }
  error_count_ = report->error_count;
  warning_count_ = report->warning_count;
  info_count_ = report->info_count;
  rules_run_ = report->rules_run;
  rules_total_ = report->rules_total;
  ran_ = true;
  state_.set_status("校验完成: error " + std::to_string(error_count_) +
                    " · warning " + std::to_string(warning_count_));
}

void ValidationPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }
  if (ImGui::Button("运行校验")) {
    run(session);
  }
  ImGui::SameLine();
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
      // 点击定位：人物问题跳到人物卡（entity_type == "person" 才有意义）
      if (i.entity_type == "person" &&
          ImGui::SmallButton(("查看##" + i.entity_id).c_str())) {
        state_.selected_person_id = i.entity_id;
      }
    }
    ImGui::EndTable();
  }
}

}  // namespace genealogy::gui
