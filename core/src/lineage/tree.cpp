#include <genealogy/lineage/tree.hpp>

#include <deque>
#include <unordered_set>

namespace genealogy::lineage {

namespace {

// visited 集合（2026-11 静态审查 H7）：环数据（child 回指祖先）会让
// 无界递归栈溢出、BFS 无限膨胀。同一人只展开一次——与家谱语义一致
// （同一人不应在同一棵子树里出现两次），并保证有环时也终止。
void expand(const PersonGraph& g, const Person* node_person,
            int depth, int max_depth, TreeNode* out,
            std::unordered_set<const Person*>& visited) {
  if (max_depth > 0 && depth >= max_depth) return;
  for (const Person* child : g.children_of(node_person->id)) {
    if (!visited.insert(child).second) continue;
    TreeNode c;
    c.person = child;
    expand(g, child, depth + 1, max_depth, &c, visited);
    out->children.push_back(std::move(c));
  }
}

}  // namespace

Result<TreeNode> descendants_tree(const PersonGraph& graph,
                                  const std::string& root_id, int max_depth) {
  const Person* root = graph.find(root_id);
  if (root == nullptr) {
    return not_found("世系树起点不在这份数据里: " + root_id);
  }
  TreeNode out;
  out.person = root;
  std::unordered_set<const Person*> visited;
  // root 必须先入 visited（与 descendants_list 同口径）：环数据里 child
  // 回指 root 时，root 会作为某子孙的子节点再次入树——不无限递归
  // （root 已 visited 会挡住再展开），但结果树出现重复节点（复核 2.7）。
  visited.insert(root);
  expand(graph, root, 1, max_depth, &out, visited);
  return out;
}

Result<std::vector<const Person*>> descendants_list(
    const PersonGraph& graph, const std::string& root_id,
    int max_depth) {
  const Person* root = graph.find(root_id);
  if (root == nullptr) {
    return not_found("世系树起点不在这份数据里: " + root_id);
  }
  std::vector<const Person*> out;
  std::unordered_set<const Person*> visited;
  std::deque<std::pair<const Person*, int>> queue;
  queue.emplace_back(root, 1);
  visited.insert(root);
  while (!queue.empty()) {
    const auto [person, depth] = queue.front();
    queue.pop_front();
    if (max_depth > 0 && depth >= max_depth) continue;
    for (const auto* child : graph.children_of(person->id)) {
      if (!visited.insert(child).second) continue;
      out.push_back(child);
      queue.emplace_back(child, depth + 1);
    }
  }
  return out;
}

std::size_t tree_node_count(const TreeNode& node) noexcept {
  std::size_t n = 1;
  for (const auto& c : node.children) n += tree_node_count(c);
  return n;
}

int tree_depth(const TreeNode& node) noexcept {
  int deepest = 1;
  for (const auto& c : node.children) {
    const int d = 1 + tree_depth(c);
    if (d > deepest) deepest = d;
  }
  return deepest;
}

}  // namespace genealogy::lineage
