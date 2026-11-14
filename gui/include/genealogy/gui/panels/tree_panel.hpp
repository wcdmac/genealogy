#pragma once

// 世系树面板：以选中人物为根的十代子孙树（core lineage::descendants_tree）。
//
// 设计文档 3.4 的「世系图画布」是自绘节点-连线（含五世一图传统版式、
// 缩放/平移/折叠），那是 M3 后续的自绘 canvas 工作。一期先用 ImGui 的
// 递归 TreeNode 把树摆出来——同样的数据、同样的展开算法，先让人
// 「看得见树」，再解决「看得漂亮」。折叠/展开由 ImGui TreeNode 免费提供。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <memory>
#include <string>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>

namespace genealogy::gui {

class TreePanel : public Panel {
 public:
  explicit TreePanel(AppState& state) : state_(state) {}

  const char* title() const override { return "世系树"; }
  void draw(DbSession& session) override;

 private:
  // 递归绘制 core 的 TreeNode。root_label 为空表示是顶层根。
  void draw_node(const lineage::TreeNode& node, const std::string& root_id);

  AppState& state_;
  int depth_ = 10;
  std::string loaded_root_;
  std::string error_;
  bool has_tree_ = false;
  // 图快照与树必须成对存活（成员持有：每帧重建太贵；选中根变了才重建）。
  // TreeNode.person 指向 graph 快照内部（tree.hpp 的存活期契约）——
  // 图若放在 draw() 的局部作用域里，函数一返回树里全是悬挂指针，
  // 之后每帧渲染都在解引用已释放内存。core 的 graph.hpp 注释里
  // 记录过服务端踩的同一个坑，GUI 不能原样再踩一次。
  std::unique_ptr<lineage::PersonGraph> graph_;
  std::unique_ptr<lineage::TreeNode> tree_;
};

}  // namespace genealogy::gui
