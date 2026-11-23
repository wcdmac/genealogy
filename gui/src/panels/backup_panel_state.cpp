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
  session.close();  // 释放 Windows 文件锁
  if (!restore(snapshot, path, force, moved_old, why)) {
    std::string reopen_why;
    session.open(path, &reopen_why);  // 失败也要尽量恢复可用会话
    return false;
  }
  if (!session.open(path, why)) return false;
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
