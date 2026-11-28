#pragma once

// 地点管理面板：地点库列表 + 增改（含历史名与经纬度）。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <genealogy/model/place.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class PlacePanel : public Panel {
 public:
  explicit PlacePanel(AppState& state) : state_(state) {}

  const char* title() const override { return "地点管理"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { clear(); }
  bool has_cached_data() const override { return !draft_.name.empty(); }

  // 无头编辑入口：与渲染层输入控件同一落点（改缓冲）
  void edit_name(const std::string& v) { name_buf_ = v; }
  void edit_modern(const std::string& v) { modern_buf_ = v; }
  void edit_historical(const std::string& v) { hist_buf_ = v; }
  void edit_lat(const std::string& v) { lat_buf_ = v; }
  void edit_lng(const std::string& v) { lng_buf_ = v; }
  void edit_note(const std::string& v) { note_buf_ = v; }
  // 把缓冲同步进 draft（渲染层保存前调用；无头测试亦经此）
  void sync_draft();

  bool reload(DbSession& session, std::string* why);
  bool save(DbSession& session, std::string* why);
  void select(const Place& p);
  void new_draft();
  void clear();

  const std::vector<Place>& rows() const { return rows_; }
  Place& draft() { return draft_; }
  const Place& draft() const { return draft_; }
  bool editing() const { return !draft_.id.empty(); }

 private:
  AppState& state_;
  std::vector<Place> rows_;
  // 每库只自动装载一次（空库避免每帧 reload，第四轮 P2）
  std::string loaded_db_;
  bool loaded_ = false;
  Place draft_;
  std::string name_buf_, modern_buf_, hist_buf_;
  std::string lat_buf_, lng_buf_, note_buf_;
};

}  // namespace genealogy::gui
