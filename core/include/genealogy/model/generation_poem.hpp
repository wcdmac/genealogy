#pragma once

// 派语辈字条目（generation_poem 表的一行）。
//
// 派语联句原文存在 lineage.generation_poem（TEXT）；按字拆分的
// 「第几句第几个字」结构化存本表——结构化的用途是辈字定位与世代
// 校验（引擎按 seq 直接查），原文留档是为了谱师校对与再生成。

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

enum class PoemOwner { Lineage, Branch };

const char* to_string(PoemOwner owner);
std::optional<PoemOwner> poem_owner_from_string(const std::string& s);

struct GenerationPoemEntry {
  std::string id;
  PoemOwner owner_type = PoemOwner::Lineage;
  std::string owner_id;
  int seq = 0;                // 辈序（第几世），从 1 起
  std::string character;      // 辈字（单字）
  std::optional<std::string> note;
};

// 归属的稳定键：仓储与 UI 都用它标识「哪一宗的派语」。
std::string poem_owner_key(PoemOwner owner, const std::string& owner_id);

nlohmann::json to_json(const GenerationPoemEntry& v);

}  // namespace genealogy
