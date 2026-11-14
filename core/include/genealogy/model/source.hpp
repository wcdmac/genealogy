#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

// 来源类型（source.type，schema CHECK 约束同口径）：谱册/方志/碑刻/
// 口述/档案/照片/基因/文书/其他。
enum class SourceType {
  GenealogyBook,
  LocalGazetteer,
  Stele,
  Oral,
  Archive,
  Photo,
  Dna,
  Document,
  Other,
};

std::string to_string(SourceType type);
std::optional<SourceType> source_type_from_string(const std::string& s);

// 来源（source 表）：一部族谱、一方碑刻、一次口述……citation 的挂靠锚点。
// 注意 source 表无 rev（溯源元数据，非谱面事实；乐观锁仅覆盖谱面实体）。
struct Source {
  std::string id;
  SourceType type = SourceType::Other;
  std::string title;                 // 必填：《高阳李氏家谱》清嘉庆4年钞本 等
  std::optional<std::string> author;
  std::optional<std::string> year_text;    // 年份照录原文（嘉庆四年 等）
  std::optional<std::string> repository;   // 收藏机构（上海图书馆 等）
  std::optional<std::string> uri;
  std::optional<std::string> citation_text;  // 规范引文原文（列名 citation）
  std::optional<std::string> note;
};

nlohmann::json to_json(const Source& v);

}  // namespace genealogy
