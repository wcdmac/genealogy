#include <genealogy/gui/panels/tree_panel.hpp>

#include <imgui.h>

#include <string>
#include <vector>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy::gui {

TreePanel::~TreePanel() { stop_worker(); }

void TreePanel::stop_worker() {
  if (worker_.joinable()) {
    ++generation_;  // 作废在途结果，让线程尽快收尾
    worker_.join();
  }
  running_ = false;
}

void TreePanel::on_database_closed() {
  stop_worker();  // 换库前停线程：旧 session 的查询不能再跑
  loaded_root_.clear();
  loaded_depth_ = 0;
  error_.clear();
  has_tree_ = false;
  graph_.reset();
  tree_.reset();
  pending_graph_.reset();
  pending_tree_.reset();
  pending_error_.clear();
}

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

  // 采纳后台结果：worker 写完 pending 并翻转 running_ 后，这里换血。
  // 图与树必须成对换（tree_ 里的 person 指针指向 graph_ 内部，
  // 二者错开一帧就是悬挂引用）。
  if (!running_ && pending_generation_ != 0) {
    stop_worker();
    graph_ = std::move(pending_graph_);
    tree_ = std::move(pending_tree_);
    error_ = std::move(pending_error_);
    has_tree_ = error_.empty() && tree_ != nullptr;
    if (!has_tree_) graph_.reset();
    pending_graph_.reset();
    pending_tree_.reset();
    pending_error_.clear();
    pending_generation_ = 0;
  }

  // 重建条件（第三轮新-C4）：根或展开代数变化都触发——此前只看根，
  // 改代数不生效，界面上的树一直是旧深度。
  if ((root != loaded_root_ || depth_ != loaded_depth_) && !running_) {
    loaded_root_ = root;
    loaded_depth_ = depth_;
    has_tree_ = false;
    error_.clear();
    tree_.reset();
    graph_.reset();

    // 后台装载（第三轮新-C10）：20 万装载 + 建图 + 展开不再冻结 UI。
    // Database 是 FULLMUTEX（serialized）模式，worker 与 UI 线程共用
    // session 的连接是安全的；换根/换库递增 generation 作废本批结果。
    ++generation_;
    const int gen = generation_.load();
    const int depth = depth_;
    running_ = true;
    DbSession* s = &session;
    worker_ = std::thread([this, s, root, depth, gen] {
      auto finish = [&](std::unique_ptr<lineage::PersonGraph> g,
                        std::unique_ptr<lineage::TreeNode> t, std::string err) {
        if (generation_.load() != gen) return;  // 已被新请求/换库作废
        pending_graph_ = std::move(g);
        pending_tree_ = std::move(t);
        pending_error_ = std::move(err);
        pending_generation_ = gen;
        running_ = false;  // 发布点（release），UI 线程此帧起可采纳
      };

      PersonFilter pf;
      pf.limit = 200000;
      auto persons = s->persons().list_persons(pf);
      if (!persons) {
        finish(nullptr, nullptr, "载入人物失败: " + persons.error().message());
        return;
      }
      UnionFilter uf;
      uf.limit = 200000;
      auto unions = s->unions().list_unions(uf);
      if (!unions) {
        finish(nullptr, nullptr, "载入结合失败: " + unions.error().message());
        return;
      }
      // 图必须活过本帧——TreeNode.person 指向快照内部（tree.hpp 契约）。
      auto graph = std::make_unique<lineage::PersonGraph>(
          lineage::PersonGraph::build(std::move(*persons), std::move(*unions)));
      auto t = lineage::descendants_tree(*graph, root, depth);
      if (!t) {
        finish(nullptr, nullptr, t.error().message());
        return;
      }
      finish(std::move(graph),
             std::make_unique<lineage::TreeNode>(std::move(*t)), "");
    });
  }

  if (running_) {
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
