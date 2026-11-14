// M1.5 校验规则引擎测试：每条规则都要有用例打中，
// 且要验证「规则表驱动」——改 JSON 就能改等级/阈值/启停。

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/validate/validation.hpp>

using namespace genealogy;
using namespace genealogy::lineage;

namespace {

Person mk(const std::string& id, const std::string& surname,
          const std::string& given, Gender gender, int birth_year,
          int death_year = 0) {
  Person p;
  p.id = id;
  p.lineage_id = "L1";
  p.family_name = surname;
  p.given_name = given;
  p.gender = gender;
  p.birth_date = GenealogyDate::from_iso(std::to_string(birth_year)).value();
  if (death_year > 0) {
    p.death_date = GenealogyDate::from_iso(std::to_string(death_year)).value();
  }
  return p;
}

int count_rule(const ValidationReport& r, const std::string& rule_id) {
  int n = 0;
  for (const auto& i : r.issues) {
    if (i.rule_id == rule_id) ++n;
  }
  return n;
}

bool has_issue(const ValidationReport& r, const std::string& rule_id,
               const std::string& entity_id) {
  for (const auto& i : r.issues) {
    if (i.rule_id == rule_id && i.entity_id == entity_id) return true;
  }
  return false;
}

ValidationReport run(std::vector<Person> persons,
                     const std::vector<Union>& unions = {},
                     const std::vector<Event>* events = nullptr) {
  const PersonGraph g = PersonGraph::build(persons, unions);
  auto r = validate(g, events);
  REQUIRE(r.has_value());
  return *r;
}

}  // namespace

TEST_CASE("校验：规则表能加载，且每条规则都有谓词实现") {
  const std::vector<Person> none;
  const auto r = run(none);
  // 15 条登记规则，1 条默认停用
  CHECK(r.rules_total == 15);
  CHECK(r.rules_run == 14);
  CHECK(r.ruleset == "genealogy-validation");
  CHECK(r.ruleset_version == "1");
  CHECK(r.issues.empty());
}

TEST_CASE("校验：R1 出生晚于卒年") {
  std::vector<Person> ps;
  ps.push_back(mk("P1", "陈", "甲", Gender::Male, 1900, 1880));   // 出生 > 卒
  ps.push_back(mk("P2", "陈", "乙", Gender::Male, 1880, 1900));   // 正常
  const auto r = run(ps);
  CHECK(has_issue(r, "R1", "P1"));
  CHECK_FALSE(has_issue(r, "R1", "P2"));
  CHECK(r.error_count >= 1);
  // 同年不算悖论
  ps.clear();
  ps.push_back(mk("P3", "陈", "丙", Gender::Male, 1900, 1900));
  CHECK_FALSE(has_issue(run(ps), "R1", "P3"));
}

TEST_CASE("校验：R2 父母出生不晚于子女过多（阈值来自规则表）") {
  std::vector<Person> ps;
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1900));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1905));
  ps.back().father_id = "F";
  const auto r = run(ps);
  CHECK(has_issue(r, "R2", "C"));
  // 只差 5 年，低于规则表的 12 年阈值

  // 差 20 年不应报
  ps.clear();
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1880));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1900));
  ps.back().father_id = "F";
  CHECK_FALSE(has_issue(run(ps), "R2", "C"));
}

TEST_CASE("校验：R3 子女出生晚于父母卒年过多（阈值来自规则表）") {
  // 母亲 1850-1880，儿子 1950 生：差 70 年 > 母亲阈值 55 → 报
  std::vector<Person> ps;
  ps.push_back(mk("M", "王", "母", Gender::Female, 1850, 1880));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1950));
  ps.back().mother_id = "M";
  CHECK(has_issue(run(ps), "R3", "C"));

  // 父亲 1840-1890，儿子 1950 生：差 60 年 < 父亲阈值 80 → 不报
  ps.clear();
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1840, 1890));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1950));
  ps.back().father_id = "F";
  CHECK_FALSE(has_issue(run(ps), "R3", "C"));

  // 父亲卒得早（1840-1870），儿子 1950 生：差 80 年，不超过 80 → 仍不报
  // 卒于 1860 则差 90 年 > 80 → 报
  ps.clear();
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1840, 1860));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1950));
  ps.back().father_id = "F";
  CHECK(has_issue(run(ps), "R3", "C"));

  // 父母都健在（无卒年）→ 无从判断，不报
  ps.clear();
  ps.push_back(mk("M", "王", "母", Gender::Female, 1850));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1950));
  ps.back().mother_id = "M";
  CHECK_FALSE(has_issue(run(ps), "R3", "C"));
}

TEST_CASE("校验：R4 自指父母") {
  std::vector<Person> ps;
  Person p = mk("P", "陈", "甲", Gender::Male, 1900);
  p.father_id = "P";
  ps.push_back(p);
  const auto r = run(ps);
  CHECK(has_issue(r, "R4", "P"));
}

TEST_CASE("校验：R5 疑似重复人物（同名同年）") {
  std::vector<Person> ps;
  ps.push_back(mk("P1", "陈", "文远", Gender::Male, 1864));
  ps.push_back(mk("P2", "陈", "文远", Gender::Male, 1864));
  ps.push_back(mk("P3", "陈", "文远", Gender::Male, 1870));   // 差 6 年
  ps.push_back(mk("P4", "陈", "文华", Gender::Male, 1864));   // 同名不同
  const auto r = run(ps);
  CHECK(count_rule(r, "R5") == 1);   // 只报一对，且同一人只报一次
  const bool p1 = has_issue(r, "R5", "P1");
  const bool p2 = has_issue(r, "R5", "P2");
  CHECK((p1 || p2));
  CHECK_FALSE(has_issue(r, "R5", "P3"));
  CHECK_FALSE(has_issue(r, "R5", "P4"));
}

TEST_CASE("校验：R6 配偶性别相同") {
  std::vector<Person> ps;
  ps.push_back(mk("H", "陈", "夫", Gender::Male, 1860));
  ps.push_back(mk("W", "陈", "妻", Gender::Male, 1862));   // 同为男
  std::vector<Union> us;
  Union u;
  u.id = "U1";
  u.members = {UnionMember{"H", MemberRole::Husband},
               UnionMember{"W", MemberRole::Wife}};
  us.push_back(u);
  const auto r = run(ps, us);
  CHECK(has_issue(r, "R6", "U1"));

  // 一夫一妻正常，不报
  ps[1].gender = Gender::Female;
  CHECK_FALSE(has_issue(run(ps, us), "R6", "U1"));
}

TEST_CASE("校验：R7 父母冗余字段与婚姻结合不一致") {
  std::vector<Person> ps;
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1860));
  ps.push_back(mk("M", "王", "母", Gender::Female, 1862));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1888));
  ps.back().father_id = "F";
  ps.back().mother_id = "M";
  // 没有 union → 冗余字段找不到依据
  const auto r = run(ps);
  CHECK(has_issue(r, "R7", "C"));

  // 补上 union 就不报了
  std::vector<Union> us;
  Union u;
  u.id = "U1";
  u.members = {UnionMember{"F", MemberRole::Husband},
               UnionMember{"M", MemberRole::Wife}};
  us.push_back(u);
  CHECK_FALSE(has_issue(run(ps, us), "R7", "C"));

  // 父母指向图外的人也要报
  ps.back().father_id = "GHOST";
  CHECK(has_issue(run(ps, us), "R7", "C"));
}

TEST_CASE("校验：R7 仅登记一方父母不误报") {
  // 单亲、妾室未录母亲、续弦未录：只登记一方时无从谈「同时包含
  // 二人」。曾因有一方就查互链，凡母亲未录者全员误报 error
  // （2026-10-23 端到端经 _CONC 妾室谱暴露）。
  std::vector<Person> ps;
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1860));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1888));
  ps.back().father_id = "F";   // 只有父亲，无结合
  CHECK_FALSE(has_issue(run(ps), "R7", "C"));

  // 只有母亲同理
  ps[1].father_id = std::nullopt;
  ps.push_back(mk("M", "王", "母", Gender::Female, 1862));
  ps[1].mother_id = "M";
  CHECK_FALSE(has_issue(run(ps), "R7", "C"));

  // 双方登记且无结合：照旧报（真不一致不能因修误报而放过）
  ps[1].father_id = "F";
  CHECK(has_issue(run(ps), "R7", "C"));
}

TEST_CASE("校验：R8/R9/R10 信息类规则") {
  std::vector<Person> ps;
  ps.push_back(mk("P1", "陈", "甲", Gender::Male, 1860));
  ps.push_back(mk("P2", "陈", "乙", Gender::Unknown, 1860));
  Person p3 = mk("P3", "陈", "丙", Gender::Male, 1860);
  p3.birth_date = GenealogyDate{};   // 空日期
  ps.push_back(p3);

  const auto r = run(ps);
  CHECK(has_issue(r, "R9", "P2"));   // 性别未登记
  CHECK(has_issue(r, "R10", "P3"));  // 生年缺失
  CHECK_FALSE(has_issue(r, "R9", "P1"));
  CHECK_FALSE(has_issue(r, "R10", "P1"));

  // R8：有父母但无世代序号
  ps.clear();
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1860));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1888));
  ps.back().father_id = "F";
  const auto r2 = run(ps);
  CHECK(has_issue(r2, "R8", "C"));
  // 补上世代序号就不报
  ps.back().generation_no = 3;
  CHECK_FALSE(has_issue(run(ps), "R8", "C"));
}

TEST_CASE("校验：R11 世系图有环") {
  std::vector<Person> ps;
  ps.push_back(mk("A", "陈", "甲", Gender::Male, 1850));
  ps.push_back(mk("B", "陈", "乙", Gender::Male, 1880));
  ps.push_back(mk("C", "陈", "丙", Gender::Male, 1910));
  ps[0].father_id = "C";   // A 的父亲是 C，C 的父亲是 B，B 的父亲是 A → 环
  ps[1].father_id = "A";
  ps[2].father_id = "B";
  const auto r = run(ps);
  CHECK(count_rule(r, "R11") >= 1);
  // 环上三人都该被点到
  CHECK(has_issue(r, "R11", "A"));
  CHECK(has_issue(r, "R11", "B"));
  CHECK(has_issue(r, "R11", "C"));
}

TEST_CASE("校验：R12 日期有原文但换算不出公历") {
  std::vector<Person> ps;
  Person p = mk("P", "陈", "甲", Gender::Male, 1860);
  p.birth_date = GenealogyDate::from_raw("同治三年四月初四").value();
  // from_raw 只照录，不换算 → 有原文、无公历
  CHECK_FALSE(p.birth_date.gregorian.has_value());
  ps.push_back(p);
  const auto r = run(ps);
  CHECK(has_issue(r, "R12", "P"));
}

TEST_CASE("校验：R13 葬期早于卒期") {
  std::vector<Person> ps;
  ps.push_back(mk("P", "陈", "甲", Gender::Male, 1860, 1900));
  std::vector<Event> evs;
  Event e;
  e.id = "E1";
  e.subject_type = EventSubjectType::Person;
  e.subject_id = "P";
  e.type = EventType::Burial;
  e.date = GenealogyDate::from_iso("1880").value();
  evs.push_back(e);
  const auto r = run(ps, {}, &evs);
  CHECK(has_issue(r, "R13", "P"));
  // 不传事件表时本条静默跳过（不报也不错报）
  CHECK_FALSE(has_issue(run(ps), "R13", "P"));
}

TEST_CASE("校验：R14 排行断层") {
  std::vector<Person> ps;
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1860));
  ps.push_back(mk("A", "陈", "甲", Gender::Male, 1888));
  ps.push_back(mk("B", "陈", "乙", Gender::Male, 1890));
  ps.push_back(mk("C", "陈", "丙", Gender::Male, 1892));
  ps[1].birth_order = 1;
  ps[2].birth_order = 2;
  ps[3].birth_order = 4;   // 缺 3
  for (auto& p : ps) {
    if (p.id != "F") p.father_id = "F";
  }
  const auto r = run(ps);
  CHECK(has_issue(r, "R14", "A"));   // 报 id 最小的同胞

  // 连续就不报
  ps[3].birth_order = 3;
  CHECK_FALSE(has_issue(run(ps), "R14", "A"));
}

TEST_CASE("校验：only_rule_ids 只跑指定规则") {
  std::vector<Person> ps;
  ps.push_back(mk("P", "陈", "甲", Gender::Unknown, 1900));   // 命中 R9
  ValidationOptions opt;
  opt.only_rule_ids = {"R9"};
  const PersonGraph g = PersonGraph::build(ps);
  const auto r = validate(g, nullptr, opt);
  REQUIRE(r.has_value());
  CHECK(r->rules_run == 1);
  CHECK(r->issues.size() == 1);
  CHECK(r->issues.front().rule_id == "R9");
}

TEST_CASE("校验：消息文案里的阈值占位符会被替换") {
  std::vector<Person> ps;
  ps.push_back(mk("F", "陈", "父", Gender::Male, 1900));
  ps.push_back(mk("C", "陈", "子", Gender::Male, 1905));
  ps.back().father_id = "F";
  const auto r = run(ps);
  for (const auto& i : r.issues) {
    if (i.rule_id == "R2") {
      CHECK(i.message.find("12") != std::string::npos);   // min_parent_age_years
      CHECK(i.message.find('{') == std::string::npos);    // 不留占位符
      CHECK(i.severity == IssueSeverity::Error);
      CHECK(i.entity_type == "person");
    }
  }
}

TEST_CASE("校验：报告按 规则序 → 实体 id 排序，输出可复现") {
  std::vector<Person> ps;
  ps.push_back(mk("P9", "陈", "甲", Gender::Unknown, 1900));
  ps.push_back(mk("P1", "陈", "乙", Gender::Unknown, 1900));
  ps.push_back(mk("P5", "陈", "丙", Gender::Unknown, 1900));
  const auto r1 = run(ps);
  const auto r2 = run(ps);
  REQUIRE(r1.issues.size() == r2.issues.size());
  for (std::size_t i = 0; i < r1.issues.size(); ++i) {
    CHECK(r1.issues[i].rule_id == r2.issues[i].rule_id);
    CHECK(r1.issues[i].entity_id == r2.issues[i].entity_id);
  }
  // 同一规则内按实体 id 升序
  for (std::size_t i = 1; i < r1.issues.size(); ++i) {
    const auto& a = r1.issues[i - 1];
    const auto& b = r1.issues[i];
    if (a.rule_id == b.rule_id) CHECK(a.entity_id < b.entity_id);
  }
}
