// 世系树面板的渲染层（ImGui）。重建决策与后台装载/采纳在
// tree_panel_state.cpp——无 imgui 依赖、可被 gui-tests 无头驱动；
// 本 TU 只把状态机的 API 画出来，不含任何装配逻辑。

#include <genealogy/gui/panels/tree_panel.hpp>

#include <imgui.h>

#include <string>
#include <vector>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>

namespace genealogy::gui {

void TreePanel::draw_node(const lineage::TreeNode& node,
                          const std::string& root_id) {
  if (node.person == nullptr) {
    ImGui::BulletText("（空节点）");
    return;
  }
  const Person& p = *node.person;
  const std::string label = p.family_name + p.given_name +
                            (p.generation_no
                                 ? "（" + std::to_string(*p.generation_no) + "世）"
                                 : std::string{});
  const bool is_root = p.id == root_id;
  const bool leaf = node.children.empty();

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
                             ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_SpanFullWidth;
  if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
  if (is_root) flags |= ImGuiTreeNodeFlags_Selected;

  ImGui::PushID(p.id.c_str());
  const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("选")) {
    state_.selected_person_id = p.id;
  }
  if (open) {
    for (const auto& c : node.children) {
      draw_node(c, root_id);
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void TreePanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }

  const std::string root = state_.selected_person_id;
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("展开代数", &depth_);
  if (depth_ < 1) depth_ = 1;
  if (depth_ > 30) depth_ = 30;
  ImGui::SameLine();
  ImGui::TextDisabled("（根: %s）", root.empty() ? "未选中" : root.c_str());

  ImGui::Separator();

  if (root.empty()) {
    ImGui::TextDisabled("在别处选中一个人物，这里展开其子孙树。");
    return;
  }

  // 采纳后台结果 + 按需重建：决策与线程管理在状态机
  // （tree_panel_state.cpp，可无头单测），这里只画。
  poll();
  if (needs_rebuild(root, depth_) && !load_in_flight()) {
    begin_load(session, root, depth_);
  }

  if (load_in_flight()) {
    ImGui::TextDisabled("装载中…（20 万级装载在后台线程跑，UI 不冻结）");
    return;
  }
  if (!error_.empty()) {
    ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "%s", error_.c_str());
    return;
  }
  if (!has_tree_ || !tree_) {
    return;
  }

  ImGui::BeginChild("tree_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
  draw_node(*tree_, root);
  ImGui::EndChild();
}

}  // namespace genealogy::gui