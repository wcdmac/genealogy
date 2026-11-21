// 世系树面板的无 UI 状态机（ROADMAP「面板装配抽离 ImGui TU」债务偿还）。
//
// 重建决策（第三轮新-C4）与后台装载/采纳（第三轮新-C10）在此成 TU：
// 无 imgui 依赖，gui-tests 直接驱动 begin_load/poll/needs_rebuild——
// 深度重建条件与换库作废语义自此可自动验证。draw() 留在
// tree_panel.cpp（渲染层），只调用这里的 API。
//
// 线程模型：worker 独占写 pending 缓冲，写完翻转 running_（release）；
// UI 线程 running_ == false 后读（acquire）——发布点即同步点。Database
// 为 FULLMUTEX（serialized）模式，worker 与 UI 线程共用 session 的
// 连接安全；AppState 只在 UI 线程写。

#include <genealogy/gui/panels/tree_panel.hpp>

#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

#include <string>
#include <utility>
#include <vector>

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

bool TreePanel::needs_rebuild(const std::string& root, int depth) const {
  // （第三轮新-C4）：根或展开代数变化都触发——此前只看根，改代数
  // 不生效，界面上的树一直是旧深度。空根没有可展开的东西。
  return !root.empty() && (root != loaded_root_ || depth != loaded_depth_);
}

void TreePanel::begin_load(DbSession& session, const std::string& root,
                           int depth) {
  loaded_root_ = root;
  loaded_depth_ = depth;
  has_tree_ = false;
  error_.clear();
  tree_.reset();
  graph_.reset();

  ++generation_;
  const int gen = generation_.load();
  running_ = true;
  DbSession* s = &session;
  worker_ = std::thread([this, s, root, depth, gen] {
    auto finish = [&](std::unique_ptr<lineage::PersonGraph> g,
                      std::unique_ptr<lineage::TreeNode> t, std::string err) {
      // 已被新请求/换库作废：不发布结果（running_ 由作废方复位）
      if (generation_.load() != gen) return;
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

void TreePanel::poll() {
  if (running_ || pending_generation_ == 0) return;
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

}  // namespace genealogy::gui
