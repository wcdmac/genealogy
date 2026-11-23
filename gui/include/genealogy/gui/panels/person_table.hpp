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

  // 无头载入入口（与 draw 中"重新载入"同一落点）
  void reload_for_test(DbSession& session) { reload(session); }

  // ---- 行内编辑（无头可测）：双击姓名列进入编辑态
  bool begin_edit(const std::string& id, std::string* why);  // 载入该人进缓冲
  bool save_edit(DbSession& session, std::string* why);      // 写回（rev 乐观锁）
  void cancel_edit();
  bool editing() const { return !edit_id_.empty(); }
  const std::string& edit_id() const { return edit_id_; }

  // 无头编辑入口（与渲染层控件同一落点）
  void edit_family(const std::string& v) { efamily_ = v; }
  void edit_given(const std::string& v) { egiven_ = v; }
  void edit_courtesy(const std::string& v) { ecourtesy_ = v; }
  void edit_generation(const std::string& v) { egen_ = v; }
  void edit_birth_order(const std::string& v) { ebirth_order_ = v; }
  void set_edit_gender(int g) { egender_ = g; }

  std::string edit_family() const { return efamily_; }
  std::string edit_given() const { return egiven_; }
  int edit_gender() const { return egender_; }

 private:
  void reload(DbSession& session);

  AppState& state_;
  std::vector<TableRow> rows_;
  std::string loaded_db_path_;   // 载入时的库路径（变了才重载）
  bool loaded_ = false;
  int gen_from_ = 0;             // 世代区间筛选（0 = 不限）
  int gen_to_ = 0;
  char name_filter_[128] = {};   // 姓名子串筛选
  int sort_col_ = 0;
  bool sort_asc_ = true;

  // 行内编辑缓冲
  std::string edit_id_;
  std::int64_t edit_rev_ = 0;
  std::string efamily_, egiven_, ecourtesy_, egen_, ebirth_order_;
  int egender_ = 2;

 public:
  void on_database_closed() override {
    rows_.clear();
    loaded_db_path_.clear();
    loaded_ = false;
  }
  // 面板是否持有任何缓存数据（换库清理断言的观测缝）
  bool has_cached_data() const override { return loaded_ || !rows_.empty() || editing(); }
};

}  // namespace genealogy::gui
