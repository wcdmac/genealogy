#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

// 支派/房（branch 表）：自关联成树，generation_char 为派语定位字
// （同姓不同支派派语不同，辈字是区分支派的机器可读指纹）。
struct Branch {
  std::string id;
  std::string name;
  std::optional<std::string> founder_person_id;  // 始迁祖
  std::optional<std::string> parent_branch_id;   // 父房
  std::optional<std::string> generation_char;
  std::optional<std::string> note;
  std::int64_t rev = 1;
};

nlohmann::json to_json(const Branch& v);

}  // namespace genealogy
