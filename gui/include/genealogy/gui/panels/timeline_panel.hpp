#pragma once

// 时间线与地点分布面板：事件按时间排序呈现，地点按事件数统计。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <genealogy/model/event.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

struct TimelineRow {
  std::string year;
  std::string type;
  std::string subject;
  std::string place;
  std::string description;
};

struct PlaceCount {
  std::string place_id;
  std::string name;
  int count = 0;
};

class TimelinePanel : public Panel {
 public:
  explicit TimelinePanel(AppState& state) : state_(state) {}

  const char* title() const override { return "时间线·地点"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { clear(); }
  bool has_cached_data() const override { return !rows_.empty(); }

  // ---- 无头状态机
  bool reload(DbSession& session, std::string* why);
  void clear();
  const std::vector<TimelineRow>& rows() const { return rows_; }
  const std::vector<PlaceCount>& places() const { return places_; }
  void set_type_filter(int idx) { type_filter_ = idx; }
  int type_filter() const { return type_filter_; }

 private:
  AppState& state_;
  std::vector<TimelineRow> rows_;
  std::vector<PlaceCount> places_;
  int type_filter_ = 0;  // 0 = 全部
};

}  // namespace genealogy::gui
