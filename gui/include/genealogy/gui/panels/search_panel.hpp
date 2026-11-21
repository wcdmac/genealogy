#pragma once

// 检索面板：FTS 全文 + 结构化筛选（支派/世代）。
// 设计文档 3.4「检索」：FTS 全文 + 结构化筛选（支派/世代/年代/事件类型/
// 有无证据）。一期实现 FTS + 支派/世代；年代/事件类型/证据随 M3 后续面板补。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

struct SearchHit {
  std::string id;
  std::string name;
  std::string gender;
  std::string birth;
  std::string death;
  int generation = 0;
};

class SearchPanel : public Panel {
 public:
  explicit SearchPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "检索"; }
  void draw(DbSession& session) override;
  // 关库/换库清结果（复核 2.13：旧库命中不再展示；查询词保留，
  // 与人物表保留筛选词同口径）+ 谱筛选列表重载
  void on_database_closed() override {
    hits_.clear();
    searched_ = false;
    lineage_options_.clear();
    loaded_lineages_for_ = false;
    lineage_filter_ = 0;
  }
  // 面板是否持有任何缓存数据（换库清理断言的观测缝）
  bool has_cached_data() const { return searched_ || !hits_.empty(); }

 private:
  void run_search(DbSession& session);

  AppState& state_;
  char query_[128] = {};
  int limit_ = 100;
  std::vector<SearchHit> hits_;
  bool searched_ = false;   // 有没有跑过一次（决定显示提示还是结果）
  // 谱筛选（2026-11-21 多谱隔离：0 = 全部谱，>0 = 谱列表下标）
  int lineage_filter_ = 0;
  bool loaded_lineages_for_ = false;  // 谱列表按库装载标记
  std::vector<std::pair<std::string, std::string>> lineage_options_;  // id/名
};

}  // namespace genealogy::gui
