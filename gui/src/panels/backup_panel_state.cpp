// 备份恢复面板状态机（无 ImGui 依赖）。

#include <genealogy/gui/panels/backup_panel.hpp>

#include <genealogy/io/backup_service.hpp>

#include <filesystem>

namespace genealogy::gui {

bool BackupPanel::backup(DbSession& session, const std::string& dir,
                         std::string* out_path, std::string* why) {
  if (!session.is_open()) { *why = "未打开数据库"; return false; }
  namespace fs = std::filesystem;
  fs::path out(dir);
  std::error_code ec;
  fs::create_directories(out, ec);
  if (ec) { *why = "建不了备份目录: " + ec.message(); return false; }
  const fs::path snapshot = out / ("genealogy-" + genealogy::io::backup_timestamp() + ".db");
  if (auto r = genealogy::io::snapshot_database(session.db(), snapshot); !r) {
    *why = r.error().message();
    return false;
  }
  *out_path = snapshot.string();
  return true;
}

bool BackupPanel::restore_and_reopen(DbSession& session,
                                     const std::string& snapshot, bool force,
                                     std::string* moved_old, std::string* why) {
  const std::string path = session.path();
  // 先停后台 worker + 清面板缓存（与菜单关库同一序列），否则
  // worker 持有的裸 DbSession* 会成为悬垂指针。
  if (state_.before_db_close) state_.before_db_close();
  session.close();  // 释放 Windows 文件锁
  if (!restore(snapshot, path, force, moved_old, why)) {
    std::string reopen_why;
    if (session.open(path, &reopen_why) && state_.after_db_open)
      state_.after_db_open(path);  // 失败也尽量恢复可用会话
    return false;
  }
  if (!session.open(path, why)) return false;
  // 恢复后重设 db_path 并通知面板重载，避免显示旧库缓存
  state_.db_path = path;
  if (state_.after_db_open) state_.after_db_open(path);
  return true;
}

bool BackupPanel::restore(const std::string& snapshot, const std::string& target,
                          bool force, std::string* moved_old, std::string* why) {
  auto r = genealogy::io::restore_database(snapshot, target, force);
  if (!r) { *why = r.error().message(); return false; }
  *moved_old = *r;
  return true;
}

}  // namespace genealogy::gui
