#pragma once

// 重复人物合并向导：检测 → 并排对照 → 冲突提示 → 合并（带快照）。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <genealogy/service/merge_service.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class MergePanel : public Panel {
 public:
  explicit MergePanel(AppState& state) : state_(state) {}

  const char* title() const override { return "重复合并"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { clear(); }
  bool has_cached_data() const override { return !pairs_.empty(); }

  bool scan(DbSession& session, std::string* why);
  bool preview(DbSession& session, const std::string& winner,
               const std::string& loser, std::string* why);
  bool merge(DbSession& session, const std::string& backup_dir,
             std::string* why);
  void clear();

  const std::vector<DuplicatePair>& pairs() const { return pairs_; }
  const std::vector<MergeConflict>& conflicts() const { return conflicts_; }
  void select_pair(int idx) { selected_ = idx; }
  int selected() const { return selected_; }
  std::string backup_path() const { return backup_path_; }

 private:
  AppState& state_;
  std::vector<DuplicatePair> pairs_;
  std::vector<MergeConflict> conflicts_;
  int selected_ = -1;
  std::string backup_path_;
};

}  // namespace genealogy::gui
