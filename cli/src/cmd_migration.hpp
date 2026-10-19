// 迁徙命令组（M5-4 补 CLI，三端共用 core 用例层）。
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

int cmd_migration(const std::vector<std::string>& args,
                  const std::string& db_path, const std::string& format);

}  // namespace genea_cli
