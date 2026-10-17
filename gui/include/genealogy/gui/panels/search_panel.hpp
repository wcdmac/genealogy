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

 private:
  void run_search(DbSession& session);

  AppState& state_;
  char query_[128] = {};
  int limit_ = 100;
  std::vector<SearchHit> hits_;
  bool searched_ = false;   // 有没有跑过一次（决定显示提示还是结果）
};

}  // namespace genealogy::gui
