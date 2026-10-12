#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

// 宗族实体（lineage 表）：谱名/姓/郡望/堂号/派语。
struct Lineage {
  std::string id;
  std::string name;
  std::string surname;
  std::optional<std::string> surname_origin;  // 姓氏源流
  std::optional<std::string> commandery;      // 郡望
  std::optional<std::string> hall_name;       // 堂号
  std::optional<std::string> generation_poem; // 派语联句原文
  std::optional<std::string> note;
  std::int64_t rev = 1;
  std::string created_at;
  std::string updated_at;
};

nlohmann::json to_json(const Lineage& v);

}  // namespace genealogy
