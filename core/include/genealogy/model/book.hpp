#pragma once

// 谱元数据（单谱单库：一库一谱，book 表恒单行、固定 id 'book'）。
// 原 lineage 维度随多谱形态裁撤（2026-11-21 定位收口）——谱名/堂号/
// 郡望/派语原文仍有意义，落在这里。

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace genealogy {

struct Book {
  std::string name;             // 谱名
  std::string surname;          // 主姓
  std::optional<std::string> surname_origin;  // 姓氏源流
  std::optional<std::string> commandery;      // 郡望
  std::optional<std::string> hall_name;       // 堂号
  std::optional<std::string> generation_poem; // 派语联句原文
  std::optional<std::string> note;
  std::string created_at;
  std::string updated_at;
  int rev = 1;
};

nlohmann::json to_json(const Book& v);

}  // namespace genealogy
