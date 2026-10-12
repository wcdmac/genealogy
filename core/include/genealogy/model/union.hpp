#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <genealogy/model/genealogy_date.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

enum class UnionType { Marriage, Concubinage, Remarriage };
enum class UnionStatus { Active, Dissolved };
enum class MemberRole { Husband, Wife, Concubine };

std::string to_string(UnionType type);
std::string to_string(UnionStatus status);
std::string to_string(MemberRole role);
std::optional<UnionType> union_type_from_string(const std::string& s);
std::optional<UnionStatus> union_status_from_string(const std::string& s);
std::optional<MemberRole> member_role_from_string(const std::string& s);

// 婚姻/结合成员（union_member 表）：union 与 person 的多对多。
struct UnionMember {
  std::string person_id;
  MemberRole role;
};

// 婚姻/结合（union 表）。SQL 中 union 为保留字，全程双引号引用。
struct Union {
  std::string id;
  UnionType type = UnionType::Marriage;
  UnionStatus status = UnionStatus::Active;
  std::optional<int> sequence;  // 第几任
  std::optional<GenealogyDate> start_date;
  std::optional<GenealogyDate> end_date;
  std::optional<std::string> place_id;
  std::optional<std::string> note;
  std::vector<UnionMember> members;
  std::int64_t rev = 1;
};

nlohmann::json to_json(const Union& v);

}  // namespace genealogy
