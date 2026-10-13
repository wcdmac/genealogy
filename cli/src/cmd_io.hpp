// genea 导入导出与校验命令组入口声明。
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

// args 已去掉命令组名，首元素为子命令（gedcom / validate）。
int cmd_io(const std::vector<std::string>& args, const std::string& db_path,
           const std::string& format);

}  // namespace genea_cli
