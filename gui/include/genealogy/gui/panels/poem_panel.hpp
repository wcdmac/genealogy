#pragma once

// 字辈派语面板：谱级/支派级派语录入、查看、一致性校验。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <genealogy/model/generation_poem.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class PoemPanel : public Panel {
 public:
  explicit PoemPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "字辈派语"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { clear(); }
  bool has_cached_data() const override { return !text_buf_.empty(); }

  // ---- 无头状态机
  bool reload(DbSession& session, std::string* why);
  bool set(DbSession& session, std::string* why);
  bool check(DbSession& session, std::string* why);
  void clear();
  void edit_text(const std::string& v) { text_buf_ = v; }
  void set_scope_branch(bool branch, const std::string& branch_id) {
    branch_scope_ = branch;
    owner_id_buf_ = branch ? branch_id : "book";
  }

  const std::vector<GenerationPoemEntry>& entries() const { return entries_; }
  const std::vector<std::string>& conflicts() const { return conflicts_; }
  const std::string& text_buf() const { return text_buf_; }
  bool branch_scope() const { return branch_scope_; }
  const std::string& owner_id() const { return owner_id_buf_; }
  void edit_branch_id(const std::string& v) { branch_id_buf_ = v; }

 private:
  AppState& state_;
  bool branch_scope_ = false;
  std::string owner_id_buf_ = "book";
  std::string text_buf_;
  std::string branch_id_buf_;  // 支派 ID 独立保存，谱级切换不覆写
  std::vector<GenerationPoemEntry> entries_;
  std::vector<std::string> conflicts_;
};

}  // namespace genealogy::gui
