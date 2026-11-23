#pragma once

// 一致性备份与恢复（M6-3 口径下沉，CLI 与 GUI 共用同一实现）。
//
// 备份 = SQLite 官方 VACUUM INTO 快照（不是 cp 活库——WAL 未合并会拷到
// 半提交状态）。恢复默认拒绝覆盖已存在的目标库；--force 时旧库连同
// -wal/-shm 旁车一起移开为 .bak，任一步失败都回滚到原位。
//
// 附件库随库走是 CLI 的职责（打包/换装涉及目录暂存），GUI 仅做主库
// 快照/恢复——界面会提示"附件需另行备份"。

#include <genealogy/error.hpp>
#include <genealogy/repository/database.hpp>

#include <filesystem>
#include <string>

namespace genealogy::io {

// 生成时间戳（本地时间，秒+毫秒，避免同秒两次撞路径）
std::string backup_timestamp();

// 一致性快照：写入 path，失败返回 IoError（带 sqlite 原始信息）
Result<void> snapshot_database(Database& db, const std::filesystem::path& path);

// 快照可读性预检（打开并 SELECT，损坏就地拒绝）
Result<void> verify_snapshot(const std::filesystem::path& path);

// 恢复快照到 target。target 已存在且 force=false → Conflict。
// force=true 时旧库（含旁车）移开为 target.bak-<时间戳>，失败回滚。
// 返回移开的旧库路径（供状态栏提示），未移开则空串。
Result<std::string> restore_database(const std::filesystem::path& snapshot,
                                     const std::filesystem::path& target,
                                     bool force);

}  // namespace genealogy::io
