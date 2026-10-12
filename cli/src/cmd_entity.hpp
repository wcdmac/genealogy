// genea 实体命令组入口声明：lineage / branch / place / union / event。
// args 已去掉命令组名（如 "lineage"），首元素为子命令（add/list/show/remove）。
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

int cmd_lineage(const std::vector<std::string>& args,
                const std::string& db_path, const std::string& format);
int cmd_branch(const std::vector<std::string>& args, const std::string& db_path,
               const std::string& format);
int cmd_place(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format);
int cmd_union(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format);
int cmd_event(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format);

}  // namespace genea_cli
