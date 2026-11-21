#pragma once

// 世系表面板：ImGuiTable 大表格，列可配（设计文档 3.4）。
// 数据一次载入后内存排序/筛选——仓储已按 id 排序，这里做列排序与
// 「当前宗族/世代」筛选，不动数据库（避免每帧一次查询）。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

struct TableRow {
  std::string id;
  std::string family_name;
  std::string given_name;
  std::string courtesy_name;
  std::string gender;
  std::string birth;
  std::string death;
  std::string generation;   // 世代序号（可空）
  std::string birth_order;  // 行几
  std::string privacy;
  std::string father_id;
  std::string mother_id;
};

class PersonTablePanel : public Panel {
 public:
  explicit PersonTablePanel(AppState& state) : state_(state) {}

  const char* title() const override { return "世系表"; }
  void draw(DbSession& session) override;

 private:
  void reload(DbSession& session);

  AppState& state_;
  std::vector<TableRow> rows_;
  std::string loaded_lineage_;   // 载入时的宗族筛选（变了才重载）
  bool loaded_ = false;
  int gen_from_ = 0;             // 世代区间筛选（0 = 不限）
  int gen_to_ = 0;
  char name_filter_[128] = {};   // 姓名子串筛选
  int sort_col_ = 0;
  bool sort_asc_ = true;

 public:
  void on_database_closed() override {
    rows_.clear();
    loaded_lineage_.clear();
    loaded_ = false;
  }
  // 面板是否持有任何缓存数据（换库清理断言的观测缝）
  bool has_cached_data() const { return loaded_ || !rows_.empty(); }
};

}  // namespace genealogy::gui
