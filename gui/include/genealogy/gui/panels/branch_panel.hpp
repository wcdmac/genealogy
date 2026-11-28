#pragma once

// 支派管理面板：支派列表 + 增改（房间/房支）。状态机与渲染分离。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <genealogy/model/branch.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class BranchPanel : public Panel {
 public:
  explicit BranchPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "支派管理"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { clear(); }
  bool has_cached_data() const override { return !draft_.name.empty(); }

  // ---- 无头状态机 API
  void edit_name(const std::string& v) { name_buf_ = v; }
  void edit_generation(const std::string& v) { generation_buf_ = v; }
  void edit_note(const std::string& v) { note_buf_ = v; }
  void sync_draft();

  bool reload(DbSession& session, std::string* why);
  bool save(DbSession& session, std::string* why);   // 新建或更新（按 draft_.id）
  void select(const Branch& b);
  void new_draft();
  void clear();

  const std::vector<Branch>& rows() const { return rows_; }
  Branch& draft() { return draft_; }
  const Branch& draft() const { return draft_; }
  bool editing() const { return !draft_.id.empty(); }

 private:
  AppState& state_;
  std::vector<Branch> rows_;
  // 每库只自动装载一次（空库避免每帧 reload，第四轮 P2）
  std::string loaded_db_;
  bool loaded_ = false;
  Branch draft_;
  std::string name_buf_;
  std::string generation_buf_;
  std::string note_buf_;
};

}  // namespace genealogy::gui
