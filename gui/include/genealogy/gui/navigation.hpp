#pragma once

// 左侧导航栏的数据层（无 ImGui 依赖，可无头测试）。
// 面板按五条工作流分组（对应 docs/GUI全面规划-2026-11-23.md）。

#include <string>
#include <vector>

namespace genealogy::gui {

struct NavGroup {
  const char* name;
  std::vector<int> panel_indices;
};

// 面板注册顺序（app.cpp::build_panels）决定的索引常量。
enum PanelIndex {
  kPersonCard = 0, kNewPerson, kExport, kBranch, kPlace, kPoem, kBackup,
  kTimeline, kAttachment, kMerge, kPersonTable, kSearch, kRelation,
  kTree, kValidation, kPanelCount
};

// 五组导航。返回静态表（生命周期同程序）。
const std::vector<NavGroup>& nav_groups();

// 布局预设：返回该预设下应打开的面板索引集合。
// 预设："录入" / "查阅" / "管理" / "排版" / "全部"
std::vector<int> layout_preset(const std::string& name);
const std::vector<std::string>& layout_preset_names();

// 面板索引是否属于某组（导航高亮用）
bool group_contains(const NavGroup& group, int index);

}  // namespace genealogy::gui
