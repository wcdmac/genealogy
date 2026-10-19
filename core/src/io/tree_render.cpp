#include <genealogy/io/tree_render.hpp>

#include <functional>
#include <sstream>
#include <string>

namespace genealogy::io {

namespace {

std::string node_id(const lineage::TreeNode& node) {
  return node.person ? node.person->id : std::string("root");
}

std::string node_label(const lineage::TreeNode& node) {
  if (!node.person) return "?";
  return node.person->family_name + node.person->given_name;
}

// 递归发射。parent 为空串表示根（不画入边）。
void emit_dot(const lineage::TreeNode& node, const std::string& parent,
              std::ostringstream& os) {
  const std::string self = node_id(node);
  if (!parent.empty()) {
    os << "  \"" << parent << "\" -> \"" << self << "\"\n";
  }
  os << "  \"" << self << "\" [label=\"" << node_label(node) << "\"]\n";
  for (const auto& child : node.children) emit_dot(child, self, os);
}

void emit_mermaid(const lineage::TreeNode& node, const std::string& parent,
                  std::ostringstream& os) {
  const std::string self = node_id(node);
  if (!parent.empty()) {
    os << "  " << parent << " --> \"" << self << "\"\n";
  }
  os << "  \"" << self << "\"[\"" << node_label(node) << "\"]\n";
  for (const auto& child : node.children) emit_mermaid(child, self, os);
}

}  // namespace

std::string render_tree(const lineage::TreeNode& tree, TreeFormat format) {
  std::ostringstream os;
  if (format == TreeFormat::Mermaid) {
    os << "graph TD\n";
    emit_mermaid(tree, "", os);
  } else {
    os << "digraph genealogy {\n  rankdir=TB;\n";
    emit_dot(tree, "", os);
    os << "}\n";
  }
  return os.str();
}

}  // namespace genealogy::io
