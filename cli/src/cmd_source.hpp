// 来源与证据引用命令组（M7.2 证据链，三端共用 core 用例层）。
//
//   genea source add --type genealogy_book --title "..." [--author ...]
//                    [--year 嘉庆四年] [--repository 上海图书馆] [--uri ...]
//                    [--citation 规范引文] [--note ...]
//   genea source list [--type genealogy_book] [--query 关键词]
//   genea source show <id>
//   genea source remove <id>            # 被引用时 409 拒绝
//   genea citation add --subject-type person --subject <id> --source <id>
//                     [--locator 卷三 p136] [--confidence high] [--note ...]
//   genea citation list [--subject-type person --subject <id> | --source <id>]
//   genea citation show <id>
//   genea citation remove <id>
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

int cmd_source(const std::vector<std::string>& args,
               const std::string& db_path, const std::string& format);
int cmd_citation(const std::vector<std::string>& args,
                 const std::string& db_path, const std::string& format);

}  // namespace genea_cli
