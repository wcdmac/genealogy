// 左侧导航栏数据层。

#include <genealogy/gui/navigation.hpp>

#include <algorithm>

namespace genealogy::gui {

const std::vector<NavGroup>& nav_groups() {
  static const std::vector<NavGroup> groups = {
      {"录入", {kNewPerson, kMerge}},
      {"查阅", {kPersonCard, kPersonTable, kSearch, kRelation, kTree,
                kTimeline, kAttachment}},
      {"管理", {kBranch, kPlace, kPoem, kValidation, kBackup}},
      {"排版", {kExport}},
  };
  return groups;
}

const std::vector<std::string>& layout_preset_names() {
  static const std::vector<std::string> names = {
      "查阅", "录入", "管理", "排版", "全部"};
  return names;
}

std::vector<int> layout_preset(const std::string& name) {
  if (name == "全部") {
    std::vector<int> all;
    for (int i = 0; i < kPanelCount; ++i) all.push_back(i);
    return all;
  }
  for (const auto& g : nav_groups())
    if (name == g.name) return g.panel_indices;
  return {};
}

bool group_contains(const NavGroup& group, int index) {
  return std::find(group.panel_indices.begin(), group.panel_indices.end(),
                   index) != group.panel_indices.end();
}

}  // namespace genealogy::gui
