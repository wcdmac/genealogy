// 规则表加载与匹配（内部实现）。
// 数据来源：shared/rules/*.json 经 cmake/EmbedText.cmake 嵌入二进制。

#include "lineage/rules.hpp"

#include <algorithm>
#include <cstdio>

#include <genealogy/lineage/rules_data.hpp>

namespace genealogy::lineage::rules {

int Conditions::specificity() const {
  int n = 0;
  if (ego_gender) ++n;
  if (ego_married) ++n;
  if (ego_is_heir) ++n;
  if (alter_married) ++n;
  if (alter_is_heir) ++n;
  return n;
}

bool Conditions::matches(const MatchContext& ctx) const {
  if (ego_gender && ctx.ego_gender != *ego_gender) return false;
  if (ego_married && ctx.ego_married != *ego_married) return false;
  if (ego_is_heir && ctx.ego_is_heir != *ego_is_heir) return false;
  if (alter_married && ctx.alter_married != *alter_married) return false;
  if (alter_is_heir && ctx.alter_is_heir != *alter_is_heir) return false;
  return true;
}

std::optional<Gender> parse_gender(const nlohmann::json& j) {
  if (j.is_string()) {
    const auto s = j.get<std::string>();
    if (s == "male") return Gender::Male;
    if (s == "female") return Gender::Female;
    if (s == "unknown") return Gender::Unknown;
  }
  return std::nullopt;
}

Conditions parse_conditions(const nlohmann::json& when) {
  Conditions c;
  if (!when.is_object()) return c;
  if (auto it = when.find("ego_gender"); it != when.end()) {
    c.ego_gender = parse_gender(*it);
  }
  if (auto it = when.find("ego_married"); it != when.end() && it->is_boolean()) {
    c.ego_married = it->get<bool>();
  }
  if (auto it = when.find("ego_is_heir"); it != when.end() && it->is_boolean()) {
    c.ego_is_heir = it->get<bool>();
  }
  if (auto it = when.find("alter_married"); it != when.end() &&
      it->is_boolean()) {
    c.alter_married = it->get<bool>();
  }
  if (auto it = when.find("alter_is_heir"); it != when.end() &&
      it->is_boolean()) {
    c.alter_is_heir = it->get<bool>();
  }
  return c;
}

// ---------- 五服规则表 ----------

const std::unordered_map<std::string, GradeInfo>& mourning_grades() {
  static const auto grades = [] {
    std::unordered_map<std::string, GradeInfo> out;
    const auto j = nlohmann::json::parse(rules_data::kMourningMingQingJson,
                                         nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
      // 静默空表曾让五服全量降级成兜底（2026-11 静态审查 H14）——
      // 嵌入数据损坏必须让运维看见
      std::fputs("[rules] 五服规则表解析失败：服等表为空，匹配将全部落空\n",
                 stderr);
      return out;
    }
    for (const auto& g : j.value("grades", nlohmann::json::array())) {
      GradeInfo info;
      info.name = g.value("name", std::string());
      info.period = g.value("period", std::string());
      info.months = g.value("months", 0);
      out[g.value("id", std::string())] = std::move(info);
    }
    return out;
  }();
  return grades;
}

const std::vector<MourningRule>& mourning_rules() {
  static const auto rules = [] {
    std::vector<MourningRule> out;
    const auto j = nlohmann::json::parse(rules_data::kMourningMingQingJson,
                                         nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
      std::fputs("[rules] 五服规则表解析失败：规则表为空，匹配将全部落空\n",
                 stderr);
      return out;
    }
    for (const auto& r : j.value("rules", nlohmann::json::array())) {
      MourningRule rule;
      rule.path = r.value("path", std::string());
      rule.grade = r.value("grade", std::string());
      rule.basis = r.value("basis", std::string());
      rule.when = parse_conditions(r.value("when", nlohmann::json::object()));
      out.push_back(std::move(rule));
    }
    return out;
  }();
  return rules;
}

const MourningRule* match_mourning(const std::string& key,
                                   const MatchContext& ctx) {
  const MourningRule* best = nullptr;
  int best_spec = -1;
  for (const auto& k : key_variants(key)) {
    for (const auto& r : mourning_rules()) {
      if (r.path != k) continue;
      if (!r.when.matches(ctx)) continue;
      const int spec = r.when.specificity();
      if (spec > best_spec) {
        best = &r;
        best_spec = spec;
      }
    }
  }
  return best;
}

// ---------- 称谓规则表 ----------

const std::vector<AppellationRule>& appellation_rules() {
  static const auto rules = [] {
    std::vector<AppellationRule> out;
    const auto j =
        nlohmann::json::parse(rules_data::kAppellationJson, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
      // 与五服表同纪律（2026-11 静态审查 H14）：静默空表让称谓全量
      // 降级成组合描述，运维无可见信号（复核 2.8 残余——前两处已加
      // 告警，此处漏改）
      std::fputs("[rules] 称谓规则表解析失败：称谓表为空，匹配将全部落空\n",
                 stderr);
      return out;
    }
    for (const auto& r : j.value("terms", nlohmann::json::array())) {
      AppellationRule rule;
      rule.path = r.value("path", std::string());
      rule.standard = r.value("standard", std::string());
      rule.colloquial = r.value("colloquial", std::string());
      rule.formal = r.value("formal", std::string());
      rule.basis = r.value("basis", std::string());
      rule.when = parse_conditions(r.value("when", nlohmann::json::object()));
      out.push_back(std::move(rule));
    }
    return out;
  }();
  return rules;
}

const AppellationRule* match_appellation(const std::string& key,
                                         const MatchContext& ctx) {
  const AppellationRule* best = nullptr;
  int best_spec = -1;
  for (const auto& k : key_variants(key)) {
    for (const auto& r : appellation_rules()) {
      if (r.path != k) continue;
      if (!r.when.matches(ctx)) continue;
      const int spec = r.when.specificity();
      if (spec > best_spec) {
        best = &r;
        best_spec = spec;
      }
    }
  }
  return best;
}

std::vector<std::string> key_variants(const std::string& key) {
  std::vector<std::string> out{key};
  // 找出所有无标记的 B/Z 段（'.'-分隔），逐段展开 +/-
  std::vector<std::string> tokens;
  std::size_t start = 0;
  while (true) {
    const std::size_t dot = key.find('.', start);
    tokens.push_back(key.substr(
        start, dot == std::string::npos ? std::string::npos : dot - start));
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  std::vector<std::size_t> wild;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "B" || tokens[i] == "Z") wild.push_back(i);
  }
  if (wild.empty() || wild.size() > 3) return out;  // 组合上限 2^3
  for (std::size_t mask = 0; mask < (std::size_t{1} << wild.size()); ++mask) {
    std::vector<std::string> t = tokens;
    for (std::size_t b = 0; b < wild.size(); ++b) {
      const std::string base = t[wild[b]];
      t[wild[b]] = base + (((mask >> b) & 1) != 0 ? "+" : "-");
    }
    std::string joined;
    for (std::size_t i = 0; i < t.size(); ++i) {
      if (i > 0) joined += '.';
      joined += t[i];
    }
    out.push_back(joined);
  }
  return out;
}

}  // namespace genealogy::lineage::rules
