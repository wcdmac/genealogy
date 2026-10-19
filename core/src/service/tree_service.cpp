#include <genealogy/service/tree_service.hpp>

#include <genealogy/service/graph_service.hpp>

namespace genealogy {

TreeService::TreeService(Database& db) : db_(db) {}

Result<std::string> TreeService::export_tree(const std::string& root_id,
                                             io::TreeFormat format,
                                             int max_depth) {
  GraphService graphs(db_);
  auto graph = graphs.load_full();
  if (!graph) return graph.error();
  // graph 与 tree 同作用域：TreeNode::person 指向 graph 内的 Person，
  // 渲染完才允许 graph 析构（分离作用域 = 悬空指针，见头文件警告）。
  auto tree = lineage::descendants_tree(*graph, root_id, max_depth);
  if (!tree) return tree.error();
  return io::render_tree(*tree, format);
}

}  // namespace genealogy
