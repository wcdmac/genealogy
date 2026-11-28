// 时间线面板渲染层（地点分布用 ImGui 原生进度条，无新依赖）。

#include <genealogy/gui/panels/timeline_panel.hpp>

#include <imgui.h>

#include <algorithm>

namespace genealogy::gui {

namespace {
const char* kTypeNames[] = {"全部", "出生", "卒", "葬", "婚姻", "迁徙",
                            "科考", "任职", "荣典", "居址", "注记"};
}

void TimelinePanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    return;
  }
  // 每库装载一次：空库时双列表恒空，旧行为每帧 60fps 连环查询（第四轮 P2）
  if (!loaded_ || loaded_db_ != state_.db_path) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("时间线读取失败: " + why, true);
    loaded_ = true;
    loaded_db_ = state_.db_path;
  }

  ImGui::SetNextItemWidth(120);
  int filter = type_filter_;
  if (ImGui::Combo("事件类型", &filter, kTypeNames, 11)) {
    type_filter_ = filter;
    std::string why;
    reload(session, &why);
  }
  ImGui::SameLine();
  if (ImGui::Button("刷新")) {
    std::string why;
    if (!reload(session, &why)) state_.set_status("时间线读取失败: " + why, true);
  }

  ImGui::SeparatorText(("事件时间线（共 " + std::to_string(rows_.size()) + " 条）").c_str());
  ImGui::BeginChild("##timeline", ImVec2(0, 240), true);
  ImGui::Columns(5, "##tl", false);
  ImGui::TextUnformatted("时间"); ImGui::NextColumn();
  ImGui::TextUnformatted("类型"); ImGui::NextColumn();
  ImGui::TextUnformatted("主体"); ImGui::NextColumn();
  ImGui::TextUnformatted("地点"); ImGui::NextColumn();
  ImGui::TextUnformatted("说明"); ImGui::NextColumn();
  ImGui::Separator();
  for (const auto& r : rows_) {
    ImGui::TextUnformatted(r.year.c_str()); ImGui::NextColumn();
    ImGui::TextUnformatted(r.type.c_str()); ImGui::NextColumn();
    ImGui::TextUnformatted(r.subject.c_str()); ImGui::NextColumn();
    ImGui::TextUnformatted(r.place.c_str()); ImGui::NextColumn();
    ImGui::TextUnformatted(r.description.c_str()); ImGui::NextColumn();
  }
  ImGui::Columns(1);
  ImGui::EndChild();

  ImGui::SeparatorText("地点分布（按事件数）");
  ImGui::BeginChild("##places", ImVec2(0, 200), true);
  int max_count = 1;
  for (const auto& p : places_) max_count = std::max(max_count, p.count);
  for (const auto& p : places_) {
    ImGui::Text("%s（%d）", p.name.c_str(), p.count);
    ImGui::ProgressBar(static_cast<float>(p.count) / max_count,
                       ImVec2(-1, 0), "");
  }
  if (places_.empty()) ImGui::TextDisabled("（无地点数据）");
  ImGui::EndChild();
}

}  // namespace genealogy::gui
