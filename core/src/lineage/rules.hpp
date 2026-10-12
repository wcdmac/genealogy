// 规则表加载与匹配（core 内部头，不随库公开）。

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>

namespace genealogy::lineage::rules {

struct MatchContext {
  std::optional<genealogy::Gender> ego_gender;
  std::optional<bool> ego_married;
  std::optional<bool> ego_is_heir;
  std::optional<bool> alter_married;
  std::optional<bool> alter_is_heir;
};

struct Conditions {
  std::optional<genealogy::Gender> ego_gender;
  std::optional<bool> ego_married;
  std::optional<bool> ego_is_heir;
  std::optional<bool> alter_married;
  std::optional<bool> alter_is_heir;

  int specificity() const;
  bool matches(const MatchContext& ctx) const;
};

Conditions parse_conditions(const nlohmann::json& when);

struct GradeInfo {
  std::string name;
  std::string period;
  int months = 0;
};

struct MourningRule {
  std::string path;
  std::string grade;
  std::string basis;
  Conditions when;
};

// 五服表：grades 为服制元数据，rules 为路径规则。
const std::unordered_map<std::string, GradeInfo>& mourning_grades();
const std::vector<MourningRule>& mourning_rules();
const MourningRule* match_mourning(const std::string& key,
                                   const MatchContext& ctx);

struct AppellationRule {
  std::string path;
  std::string standard;
  std::string colloquial;
  std::string formal;
  std::string basis;
  Conditions when;
};

const std::vector<AppellationRule>& appellation_rules();
const AppellationRule* match_appellation(const std::string& key,
                                         const MatchContext& ctx);

// 键变体展开：长幼未知时 relationship_key 产出无标记的 B/Z，
// 匹配时展开为 B+/B-（Z+/Z-）全部组合（上限 8 种，防爆）。
std::vector<std::string> key_variants(const std::string& key);

}  // namespace genealogy::lineage::rules
