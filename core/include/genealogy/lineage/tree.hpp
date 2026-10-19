#pragma once

// 世系树查询（设计文档 6.1「十代子孙树查询 < 200 ms」的验收对象）。
//
// 输入是 PersonGraph 快照，纯函数、无副作用、可单测。查库与组树分开：
// 仓储层负责把数据取出来（M1 是全量装载，万级规模足够），本模块只负责
// 在内存里把父子边组织成树。

#include <cstddef>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 树节点。person 指向 graph 快照内的人物，调用方须保证 graph 存活期。
struct TreeNode {
  const ::genealogy::Person* person = nullptr;
  std::vector<TreeNode> children;
};

// load_descendants_tree 的返回：失败时是 Error。
using TreeNodeOrError = Result<TreeNode>;

// 以 root_id 为起点向下展开后代树。
//   max_depth = 1 表示只有根节点；<= 0 表示不限制（危险，可能有环）。
// 子节点排序与 PersonGraph::children_of 一致（行几 → 出生日期 → id），
// 因此同一份数据永远长出同一棵树（快照测试的前提）。
Result<TreeNode> descendants_tree(const PersonGraph& graph,
                                  const std::string& root_id, int max_depth);

// 扁平后代列表（不含根），按广度优先。给「十代有多少人」这类统计用。
Result<std::vector<const ::genealogy::Person*>> descendants_list(
    const PersonGraph& graph, const std::string& root_id, int max_depth);

// 树的最大深度与节点总数（自检用：确认展开层数符合预期）。
std::size_t tree_node_count(const TreeNode& node) noexcept;
int tree_depth(const TreeNode& node) noexcept;

}  // namespace genealogy::lineage
