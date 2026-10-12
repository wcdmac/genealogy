#pragma once

#include <genealogy/repository/database.hpp>

namespace genealogy {

// M0 便捷函数：确保库中存在默认宗族（固定 ULID 全零），返回其 id。
// M1 提供 lineage 管理命令（含自定义宗族创建）后废弃。
constexpr const char* kDefaultLineageId = "00000000000000000000000000";

void ensure_default_lineage(Database& db);

}  // namespace genealogy
