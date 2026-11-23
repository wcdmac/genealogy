// 重复合并面板状态机（无 ImGui 依赖）。

#include <genealogy/gui/panels/merge_panel.hpp>

#include <genealogy/service/merge_service.hpp>

namespace genealogy::gui {

bool MergePanel::scan(DbSession& session, std::string* why) {
  genealogy::MergeService svc(session.db());
  auto pairs = svc.detect(200);
  if (!pairs) { *why = pairs.error().message(); return false; }
  pairs_ = *pairs;
  selected_ = pairs_.empty() ? -1 : 0;
  conflicts_.clear();
  return true;
}

bool MergePanel::preview(DbSession& session, const std::string& winner,
                         const std::string& loser, std::string* why) {
  genealogy::MergeService svc(session.db());
  auto found = svc.inspect(winner, loser);
  if (!found) { *why = found.error().message(); return false; }
  conflicts_ = *found;
  return true;
}

bool MergePanel::merge(DbSession& session, const std::string& backup_dir,
                       std::string* why) {
  if (selected_ < 0 || selected_ >= static_cast<int>(pairs_.size())) {
    *why = "请先选择一对重复人物";
    return false;
  }
  const auto& pair = pairs_[selected_];
  genealogy::MergeService svc(session.db());
  auto result = svc.merge(pair.winner_id, pair.loser_id, false, backup_dir);
  if (!result) { *why = result.error().message(); return false; }
  backup_path_ = result->backup_path;
  state_.selected_person_id = pair.winner_id;
  return scan(session, why);  // 合并后重新检测
}

void MergePanel::clear() {
  pairs_.clear();
  conflicts_.clear();
  selected_ = -1;
  backup_path_.clear();
}

}  // namespace genealogy::gui
