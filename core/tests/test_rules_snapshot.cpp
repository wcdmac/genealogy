// 规则表快照测试（M4 验收项：「五服规则表快照测试通过」）。
//
// 两层覆盖：
//   1. 结构不变量——对规则表**全部条目**逐一断言（path 格式合法、
//      grade 在 grades 元数据里、服期月数 > 0、依据非空、无重复键）。
//      加新规则不会让这些断言变脆，但规则文件写坏（错别字、断链）
//      会立刻红。
//   2. 礼制结论快照——对历史上不能错的关键关系逐条钉死（子为父
//      斩衰 27 月、孙为祖父母齐衰不杖期 13 月、承重孙斩衰……）。
//      改这些数字必须显式改测试——礼制结论不是「顺手调一下」的事。
//
// 覆盖 shared/rules/mourning_*.json 与 appellation.json（构建期嵌入）。

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <genealogy/model/person.hpp>

#include "../src/lineage/rules.hpp"

using namespace genealogy;
using namespace genealogy::lineage::rules;

namespace {

// path 是点分步进格式（如 F.B+.S+ = 父之兄之子），与 relationship.cpp 的
// relationship_key 输出同构：步字母 F/M/S/D/H/W/B/Z + 长幼标记 +/-，
// 步与步之间用 . 分隔。首尾不能是点、不能有空步（F..S 非法）。
bool valid_path(const std::string& path) {
  if (path.empty()) return false;
  if (path.front() == '.' || path.back() == '.') return false;
  for (const char c : path) {
    const bool ok = (c == 'F' || c == 'M' || c == 'S' || c == 'D' ||
                     c == 'H' || c == 'W' || c == 'Q' || c == 'B' ||
                     c == 'Z' || c == '+' || c == '-' || c == '.');
    if (!ok) return false;
  }
  return path.find("..") == std::string::npos;
}

const MourningRule* find_mourning(const std::string& path) {
  const auto& rules = mourning_rules();
  for (const auto& r : rules) {
    if (r.path == path && r.when.ego_is_heir.has_value() == false &&
        !r.when.ego_gender.has_value() && !r.when.ego_married.has_value()) {
      return &r;
    }
  }
  return nullptr;
}

// 带条件的规则也要能按 (path, 条件) 定位
const MourningRule* find_mourning_when(const std::string& path,
                                       std::optional<bool> heir,
                                       std::optional<Gender> ego_gender,
                                       std::optional<bool> married) {
  for (const auto& r : mourning_rules()) {
    if (r.path != path) continue;
    if (r.when.ego_is_heir != heir) continue;
    if (r.when.ego_gender != ego_gender) continue;
    if (r.when.ego_married != married) continue;
    return &r;
  }
  return nullptr;
}

const AppellationRule* find_appellation(const std::string& path) {
  for (const auto& r : appellation_rules()) {
    if (r.path == path && !r.when.ego_gender.has_value() &&
        !r.when.ego_married.has_value() && !r.when.ego_is_heir.has_value()) {
      return &r;
    }
  }
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- 结构不变量

TEST_CASE("规则表快照：五服规则结构不变量（全量）") {
  const auto& rules = mourning_rules();
  REQUIRE(!rules.empty());

  const auto& grades = mourning_grades();
  REQUIRE(!grades.empty());

  std::set<std::string> keys;
  for (const auto& r : rules) {
    // path 合法
    INFO("path=" << r.path);
    CHECK(valid_path(r.path));
    // grade 必须在 grades 元数据里（断链 = 渲染不出服名与服期）
    INFO("grade=" << r.grade);
    REQUIRE(grades.count(r.grade) == 1);
    // 服期元数据自洽。月数允许 0：袒免与无服本来就是「无服期」，
    // 把它们当数据错误报是误解礼制——0 是结论，不是缺失。
    CHECK(grades.at(r.grade).months >= 0);
    CHECK_FALSE(grades.at(r.grade).name.empty());
    CHECK_FALSE(grades.at(r.grade).period.empty());
    // 依据非空：谱学顾问要能溯源
    CHECK_FALSE(r.basis.empty());
    // (path, 全条件组合) 唯一：重复键会让匹配结果依赖遍历顺序。
    // 条件必须五个都进键——漏掉 alter_married/alter_is_heir 时，
    // 「同 path 不同 alter 条件」的两条会被误判成重复。
    auto flag = [](const std::optional<bool>& b) {
      return !b.has_value() ? "-" : (*b ? "1" : "0");
    };
    const std::string key =
        r.path + "|" + flag(r.when.ego_is_heir) +
        (r.when.ego_gender ? std::to_string(static_cast<int>(*r.when.ego_gender))
                           : "-") +
        flag(r.when.ego_married) + flag(r.when.alter_married) +
        flag(r.when.alter_is_heir);
    INFO("key=" << key);
    CHECK(keys.insert(key).second);
  }
}

TEST_CASE("规则表快照：称谓规则结构不变量（全量）") {
  const auto& rules = appellation_rules();
  REQUIRE(!rules.empty());
  std::set<std::string> keys;
  for (const auto& r : rules) {
    INFO("path=" << r.path);
    CHECK(valid_path(r.path));
    // 三种称谓至少要有一种（兜底描述不算规则表收录）
    CHECK((!r.standard.empty() || !r.colloquial.empty() || !r.formal.empty()));
    CHECK_FALSE(r.basis.empty());
    auto flag = [](const std::optional<bool>& b) {
      return !b.has_value() ? "-" : (*b ? "1" : "0");
    };
    const std::string key =
        r.path + "|" + flag(r.when.ego_is_heir) +
        (r.when.ego_gender ? std::to_string(static_cast<int>(*r.when.ego_gender))
                           : "-") +
        flag(r.when.ego_married) + flag(r.when.alter_married) +
        flag(r.when.alter_is_heir);
    INFO("key=" << key);
    CHECK(keys.insert(key).second);
  }
}

TEST_CASE("规则表快照：五服等级元数据") {
  const auto& grades = mourning_grades();
  // 明清定型的十等等级必须齐（斩衰到缌麻）
  for (const char* id : {"zhancui", "qicui_zhang", "qicui_buzhang",
                         "qicui_wuyue", "qicui_sanyue", "dagong",
                         "xiaogong", "sima", "tanmian", "wufu"}) {
    INFO(id);
    REQUIRE(grades.count(id) == 1);
    // 袒免与无服为 0 月（无服期），其余等级必须 > 0
    const bool zero_ok = std::string(id) == "tanmian" || std::string(id) == "wufu";
    CHECK((grades.at(id).months > 0 || zero_ok));
  }
  // 斩衰三年 = 27 月（三年实为 25+1 加闰，礼制口径 27）
  CHECK(grades.at("zhancui").months == 27);
  // 缌麻三月；袒免与无服为 0（无服期）——见结构不变量里的说明
  CHECK(grades.at("sima").months == 3);
  CHECK(grades.at("tanmian").months == 0);
  CHECK(grades.at("wufu").months == 0);
}

// ---------------------------------------------------------------- 礼制结论快照

TEST_CASE("规则表快照：核心五服结论（明清定型版）") {
  // 子为父：斩衰三年（27 月）
  const auto* f = find_mourning("F");
  REQUIRE(f != nullptr);
  CHECK(f->grade == "zhancui");
  CHECK(mourning_grades().at(f->grade).months == 27);

  // 孙为祖父母（普通孙）：齐衰不杖期 13 月
  const auto* ff = find_mourning("F.F");
  REQUIRE(ff != nullptr);
  CHECK(ff->grade == "qicui_buzhang");
  CHECK(mourning_grades().at(ff->grade).months == 13);

  // 承重孙为祖父：斩衰 27 月（嫡嗣承祖父之宗）
  const auto* heir = find_mourning_when("F.F", true, std::nullopt, std::nullopt);
  REQUIRE(heir != nullptr);
  CHECK(heir->grade == "zhancui");
  CHECK(mourning_grades().at(heir->grade).months == 27);

  // 曾祖父母：齐衰五月
  const auto* fff = find_mourning("F.F.F");
  REQUIRE(fff != nullptr);
  CHECK(fff->grade == "qicui_wuyue");
  CHECK(mourning_grades().at(fff->grade).months == 5);

  // 已嫁女为父：降为期亲（齐衰不杖期）
  const auto* married_daughter =
      find_mourning_when("F", std::nullopt, Gender::Female, true);
  REQUIRE(married_daughter != nullptr);
  CHECK(married_daughter->grade == "qicui_buzhang");

  // 妻为夫：斩衰三年（H = 经丈夫步）
  const auto* h = find_mourning("H");
  REQUIRE(h != nullptr);
  CHECK(h->grade == "zhancui");
  CHECK(mourning_grades().at(h->grade).months == 27);

  // 夫为妻：齐衰杖期 13 月。明清定制夫妻服制**不对称**——
  // 妻为夫斩衰、夫为妻齐衰，把它写成对称的是用现代观念改古礼。
  const auto* w = find_mourning("W");
  REQUIRE(w != nullptr);
  CHECK(w->grade == "qicui_zhang");
  CHECK(mourning_grades().at(w->grade).months == 13);
}

TEST_CASE("规则表快照：核心称谓结论") {
  const auto* father = find_appellation("F");
  REQUIRE(father != nullptr);
  CHECK(father->standard == "父亲");

  const auto* grandpa = find_appellation("F.F");
  REQUIRE(grandpa != nullptr);
  CHECK(grandpa->standard == "祖父");

  // 兄弟：B+ 为哥哥（书面「兄」）
  const auto* elder_brother = find_appellation("B+");
  REQUIRE(elder_brother != nullptr);
  CHECK(elder_brother->standard == "哥哥");
  CHECK(elder_brother->formal == "兄");

  // 堂兄：F.B+.S+ = 父之兄之子（从父兄）。路径口径与
  // relationship_key 一致——先上溯到父、再经伯、再下降到其子。
  const auto* cousin = find_appellation("F.B+.S+");
  REQUIRE(cousin != nullptr);
  CHECK(cousin->standard == "堂兄");
  CHECK(cousin->colloquial == "堂哥");
  CHECK(cousin->formal == "从父兄");

  // 表兄（母之兄弟之子）：M.B+.S+，书面称舅表兄
  const auto* maternal_cousin = find_appellation("M.B+.S+");
  REQUIRE(maternal_cousin != nullptr);
  CHECK(maternal_cousin->standard == "表兄");
  CHECK(maternal_cousin->formal == "舅表兄");
}

TEST_CASE("规则表快照：键变体展开防爆") {
  // 无标记的 B 展开为 B+/B- 两种（外加 Z 的对应），上限 8 种
  const auto variants = key_variants("F.B");
  CHECK(!variants.empty());
  CHECK(variants.size() <= 8);
  bool has_plus = false;
  bool has_minus = false;
  for (const auto& v : variants) {
    if (v.find("B+") != std::string::npos) has_plus = true;
    if (v.find("B-") != std::string::npos) has_minus = true;
  }
  CHECK(has_plus);
  CHECK(has_minus);
}
