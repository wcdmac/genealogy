// genea poem：派语（字辈）命令组。set / show / check。
//
// 退出码约定见 cli_util.hpp（0 成功 / 1 运行时 / 2 用法 / 3 不存在 / 4 冲突）。
// check 有冲突时返回 1（与 io validate 一致，可直接当 CI 卡点）。

#pragma once

#include <string>
#include <vector>

namespace genea_cli {

int cmd_poem(const std::vector<std::string>& args, const std::string& db_path,
             const std::string& format);

}  // namespace genea_cli
