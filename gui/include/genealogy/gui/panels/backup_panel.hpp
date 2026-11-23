#pragma once

// 备份恢复面板：GUI 入口，核心逻辑复用 core::io 快照/恢复服务。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>

namespace genealogy::gui {

class BackupPanel : public Panel {
 public:
  explicit BackupPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "备份恢复"; }
  void draw(DbSession& session) override;
  void on_database_closed() override {}
  bool has_cached_data() const override { return false; }

  // ---- 无头状态机
  bool backup(DbSession& session, const std::string& dir,
              std::string* out_path, std::string* why);
  // 恢复：先关闭当前会话（Windows 文件锁），恢复后重开同一路径。
  // 返回重开是否成功；失败原因写 why。
  bool restore_and_reopen(DbSession& session, const std::string& snapshot,
                          bool force, std::string* moved_old, std::string* why);
  // 纯恢复（调用方自行保证目标未被占用）
  bool restore(const std::string& snapshot, const std::string& target,
               bool force, std::string* moved_old, std::string* why);

 private:
  AppState& state_;
  std::string dir_buf_ = "backups";
  std::string snap_buf_;
  bool force_ = false;
};

}  // namespace genealogy::gui
