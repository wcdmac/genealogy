#pragma once

#include <genealogy/error.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

// M0 便捷函数：确保库中存在默认宗族（固定 ULID 全零），返回其 id。
// M1 提供 lineage 管理命令（含自定义宗族创建）后废弃。
constexpr const char* kDefaultLineageId = "00000000000000000000000000";

// 失败（库只读/磁盘满等）返回错误——此前 sqlite3_exec 返回值被丢弃，
// 写不进去也静默成功，启动日志与库状态会不一致。
Result<void> ensure_default_lineage(Database& db);

}  // namespace genealogy
