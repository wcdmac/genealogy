#pragma once

// 世系树面板：以选中人物为根的十代子孙树（core lineage::descendants_tree）。
//
// 设计文档 3.4 的「世系图画布」是自绘节点-连线（含五世一图传统版式、
// 缩放/平移/折叠），那是 M3 后续的自绘 canvas 工作。一期先用 ImGui 的
// 递归 TreeNode 把树摆出来——同样的数据、同样的展开算法，先让人
// 「看得见树」，再解决「看得漂亮」。折叠/展开由 ImGui TreeNode 免费提供。
//
// 装载异步化（第三轮新-C10）：20 万人物装载 + 建图 + 展开是秒级操作，
// 同步跑在 draw() 里整帧冻结。改后台线程 + 代际守卫——结果落 pending
// 缓冲、以 running_ 翻转为发布点，UI 线程采纳；换根/换深度/换库都会
// 递增 generation 让旧结果作废（事实上的取消）。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>

namespace genealogy::gui {

class TreePanel : public Panel {
 public:
  explicit TreePanel(AppState& state) : state_(state) {}
  ~TreePanel() override;

  const char* title() const override { return "世系树"; }
  void draw(DbSession& session) override;
  void on_database_closed() override;

  // ---- 无头状态机（ROADMAP 债务偿还：装载/重建决策与 ImGui 绘制分离，
  // 定义在 tree_panel_state.cpp——无 imgui 依赖，gui-tests 直接驱动）。
  // 重建条件（第三轮新-C4）：根或展开代数变化都触发
  bool needs_rebuild(const std::string& root, int depth) const;
  // 后台装载（第三轮新-C10）：spawn worker；换根/换库/析构会作废
  // 在途结果（代际守卫）。Database 为 FULLMUTEX，worker 与 UI 线程
  // 共用 session 连接安全。
  void begin_load(DbSession& session, const std::string& root, int depth);
  // 采纳已完成的 worker 结果（有则换血图与树、写 error_）
  void poll();
  bool load_in_flight() const { return running_; }
  // 面板是否持有任何缓存数据（换库清理断言的观测缝）
  bool has_cached_data() const override {
    return has_tree_ || !loaded_root_.empty() || graph_ != nullptr;
  }
  // 测试观测缝：装载结果（has_tree_ 为真时 tree_ 非空）
  bool tree_ready() const { return has_tree_; }
  const std::string& last_error() const { return error_; }

 private:
  // 递归绘制 core 的 TreeNode。root_label 为空表示是顶层根。
  void draw_node(const lineage::TreeNode& node, const std::string& root_id);
  // 停掉在跑的后台装载线程（换库/析构前必须 join）
  void stop_worker();

  AppState& state_;
  int depth_ = 10;
  std::string loaded_root_;
  int loaded_depth_ = 0;  // 与 loaded_root_ 一起构成重建条件（第三轮新-C4）
  std::string error_;
  bool has_tree_ = false;
  // 图快照与树必须成对存活（成员持有：每帧重建太贵；选中根变了才重建）。
  // TreeNode.person 指向 graph 快照内部（tree.hpp 的存活期契约）——
  // 图若放在 draw() 的局部作用域里，函数一返回树里全是悬挂指针，
  // 之后每帧渲染都在解引用已释放内存。core 的 graph.hpp 注释里
  // 记录过服务端踩的同一个坑，GUI 不能原样再踩一次。
  std::unique_ptr<lineage::PersonGraph> graph_;
  std::unique_ptr<lineage::TreeNode> tree_;

  // 后台装载（第三轮新-C10）
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<int> generation_{0};  // 请求代际：变更即作废在途结果
  // pending 缓冲：worker 独占写，写完才翻转 running_（release）；
  // UI 线程在 running_ == false 后读（acquire）——发布点即同步点
  std::unique_ptr<lineage::PersonGraph> pending_graph_;
  std::unique_ptr<lineage::TreeNode> pending_tree_;
  std::string pending_error_;
  int pending_generation_ = 0;
};

}  // namespace genealogy::gui
