#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <genealogy/model/genealogy_date.hpp>

namespace genealogy {

// 名号类型（person_name.name_type，schema CHECK 同口径）：谱名/乳名/
// 学名/改后名/身后名——老谱一人多名的变更史（设计文档 4.1 实体图）。
// 字/号不在本域：person 表 courtesy_name/art_name 承担日常维护。
enum class PersonNameType {
  Genealogy,    // 谱名
  Infant,       // 乳名
  School,       // 学名
  Renamed,      // 改后名
  Posthumous,   // 身后名（谥/赠之外的名号记录）
};

std::string to_string(PersonNameType type);
std::optional<PersonNameType> person_name_type_from_string(
    const std::string& s);

// 名号（person_name 表）：Person 的 1:N 名号变更史。无 rev（台账语义，
// 修正=删了重录）。is_primary 每人至多一条=1（partial unique index 兜底）。
struct PersonName {
  std::string id;
  std::string person_id;
  std::string name;      // 必填
  PersonNameType type = PersonNameType::Genealogy;
  std::optional<GenealogyDate> start_date;  // 起用（常只记年份）
  std::optional<GenealogyDate> end_date;    // 停用
  bool is_primary = false;                  // 此人当前所用之名
  std::optional<std::string> note;
};

nlohmann::json to_json(const PersonName& v);

}  // namespace genealogy
