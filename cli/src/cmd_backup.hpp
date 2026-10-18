// 备份 / 恢复命令组（M6-3）。
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

// 一致性快照（VACUUM INTO）+ 附件库随行。args 解析 --out <目录>。
int cmd_db_backup(const std::vector<std::string>& args,
                  const std::string& db_path, const std::string& format);

// 从备份目录恢复（取最新快照）。args 解析 --from <目录> 与 --force
// （目标已存在时拒盖；--force 把旧库移开为 .bak 而非删除）。
int cmd_db_restore(const std::vector<std::string>& args,
                   const std::string& db_path, const std::string& format);

}  // namespace genea_cli
