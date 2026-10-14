#include <genealogy/lineage/tree.hpp>

#include <deque>

namespace genealogy::lineage {

namespace {

void expand(const PersonGraph& g, const Person* node_person,
            int depth, int max_depth, TreeNode* out) {
  if (max_depth > 0 && depth >= max_depth) return;
  for (const Person* child : g.children_of(node_person->id)) {
    TreeNode c;
    c.person = child;
    expand(g, child, depth + 1, max_depth, &c);
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
  expand(graph, root, 1, max_depth, &out);
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
  std::deque<std::pair<const Person*, int>> queue;
  queue.emplace_back(root, 1);
  while (!queue.empty()) {
    const auto [person, depth] = queue.front();
    queue.pop_front();
    if (max_depth > 0 && depth >= max_depth) continue;
    for (const auto* child : graph.children_of(person->id)) {
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
