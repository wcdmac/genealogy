#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>

namespace genealogy {

enum class Gender { Male, Female, Unknown };

enum class PrivacyLevel { Public, Kin, Private };

// 人物实体（设计文档 4.2 person 表）。领域层为普通 struct，
// 行映射在仓储层，JSON 映射在此（供 API DTO 边界复用）。
struct Person {
  std::string id;                   // ULID
  std::string lineage_id;           // 所属宗族
  std::optional<std::string> branch_id;
  std::string family_name;
  std::string given_name;
  std::optional<std::string> courtesy_name;     // 字
  std::optional<std::string> art_name;          // 号
  std::optional<std::string> posthumous_title;  // 谥 / 赠
  std::optional<std::string> alias;             // 别称 / 改名前名
  Gender gender = Gender::Unknown;
  std::optional<int> birth_order;    // 行几
  std::optional<int> generation_no;  // 世代序号
  bool is_heir = false;              // 承重 / 嫡嗣
  bool is_living = false;            // 在世保护
  PrivacyLevel privacy = PrivacyLevel::Kin;
  GenealogyDate birth_date;          // 未知时为空值对象
  std::optional<GenealogyDate> death_date;
  std::optional<std::string> birth_place_id;
  std::optional<std::string> death_place_id;
  std::optional<std::string> burial_place_id;
  std::optional<std::string> biography;  // 世系录 / 行传
  std::optional<std::string> father_id;
  std::optional<std::string> mother_id;
  std::optional<std::string> portrait_attachment_id;
  std::optional<std::string> note;
  std::int64_t rev = 1;       // 乐观锁
  std::string created_at;     // ISO 8601 UTC
  std::string updated_at;     // ISO 8601 UTC
};

std::string to_string(Gender gender);
std::string to_string(PrivacyLevel level);
Result<Gender> gender_from_string(const std::string& s);
Result<PrivacyLevel> privacy_from_string(const std::string& s);

nlohmann::json to_json(const Person& person);
Result<Person> person_from_json(const nlohmann::json& j);

}  // namespace genealogy
