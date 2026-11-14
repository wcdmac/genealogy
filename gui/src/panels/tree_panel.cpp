#include <genealogy/gui/panels/tree_panel.hpp>

#include <imgui.h>

#include <string>
#include <vector>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

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

  // 选中根变了才重建（ descendants_tree 每次全量展开，万级节点不能每帧跑）
  if (root != loaded_root_) {
    loaded_root_ = root;
    has_tree_ = false;
    error_.clear();
    // 旧树先摘掉再动图：tree_ 里的 person 指针指向 graph_ 内部，
    // 二者必须成对换血，否则重建窗口期内还有悬挂引用。
    tree_.reset();
    graph_.reset();

    PersonFilter pf;
    pf.limit = 200000;
    auto persons = session.persons().list_persons(pf);
    if (!persons) {
      error_ = "载入人物失败: " + persons.error().message();
      return;
    }
    UnionFilter uf;
    uf.limit = 200000;
    auto unions = session.unions().list_unions(uf);
    if (!unions) {
      error_ = "载入结合失败: " + unions.error().message();
      return;
    }
    // 图必须活过本帧——TreeNode.person 指向快照内部（tree.hpp 契约），
    // 所以 graph 进成员而不是局部变量。局部变量的版本每次 draw 都在
    // 解引用已析构的图，症状是世系树面板偶尔画出乱码人名或直接崩。
    graph_ = std::make_unique<lineage::PersonGraph>(
        lineage::PersonGraph::build(std::move(*persons), std::move(*unions)));

    auto t = lineage::descendants_tree(*graph_, root, depth_);
    if (!t) {
      error_ = t.error().message();
      graph_.reset();
      return;
    }
    tree_ = std::make_unique<lineage::TreeNode>(std::move(*t));
    has_tree_ = true;
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
