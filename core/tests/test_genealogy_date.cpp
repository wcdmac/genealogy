#include <catch2/catch_test_macros.hpp>

#include <string>

#include <nlohmann/json.hpp>

#include <genealogy/model/genealogy_date.hpp>

using namespace genealogy;

TEST_CASE("GenealogyDate：ISO 全精度", "[date]") {
  const auto d = GenealogyDate::from_iso("1864-05-09");
  REQUIRE(d);
  REQUIRE(d->calendar == Calendar::Gregorian);
  REQUIRE(d->gregorian.value() == "1864-05-09");
  REQUIRE(d->precision == DatePrecision::Day);
  REQUIRE(d->gregorian_year().value() == 1864);
  REQUIRE(d->year_ganzhi().value() == "甲子");  // 1864 = 同治三年
  REQUIRE_FALSE(d->is_empty());
  REQUIRE(d->sort_key().value() == "1864-05-09");
}

TEST_CASE("GenealogyDate：ISO 年/月精度与非法输入", "[date]") {
  const auto y = GenealogyDate::from_iso("1864");
  REQUIRE(y);
  REQUIRE(y->precision == DatePrecision::Year);

  const auto m = GenealogyDate::from_iso("1864-05");
  REQUIRE(m);
  REQUIRE(m->precision == DatePrecision::Month);

  REQUIRE_FALSE(GenealogyDate::from_iso("1864-13-01"));  // 月份越界
  REQUIRE_FALSE(GenealogyDate::from_iso("1864-02-30"));  // 2 月 30 日
  REQUIRE_FALSE(GenealogyDate::from_iso("1864-5-9"));    // 非补零
  REQUIRE_FALSE(GenealogyDate::from_iso("not-a-date"));
  REQUIRE_FALSE(GenealogyDate::from_iso(""));

  const auto bad = GenealogyDate::from_iso("1864-13-01").error();
  REQUIRE(bad.code() == ErrorCode::InvalidArgument);
}

TEST_CASE("GenealogyDate：老谱原文照录，不猜测换算", "[date]") {
  const auto d = GenealogyDate::from_raw("同治三年四月初四");
  REQUIRE(d);
  REQUIRE(d->calendar == Calendar::Lunar);
  REQUIRE(d->lunar.value() == "同治三年四月初四");
  REQUIRE_FALSE(d->gregorian.has_value());
  REQUIRE(d->precision == DatePrecision::Fuzzy);
  REQUIRE_FALSE(GenealogyDate::from_raw("   "));
  REQUIRE_FALSE(GenealogyDate::from_raw(""));
}

TEST_CASE("GenealogyDate：JSON 往返", "[date]") {
  auto d = GenealogyDate::from_iso("2024-03-10");
  REQUIRE(d);
  REQUIRE(d->year_ganzhi().value() == "甲辰");  // 2024 = 甲辰年
  d->era = Era{"民国", 113};   // 2024 = 民国 113（年号名须在年号表中存在）
  d->lunar = "二月初一";

  const auto j = d->to_json();
  const auto back = GenealogyDate::from_json(j);
  REQUIRE(back);
  REQUIRE(back->gregorian.value() == "2024-03-10");
  REQUIRE(back->precision == DatePrecision::Day);
  REQUIRE(back->era.has_value());
  REQUIRE(back->era->name == "民国");
  REQUIRE(back->lunar.value() == "二月初一");
  REQUIRE(back->year_ganzhi().value() == "甲辰");

  REQUIRE_FALSE(GenealogyDate::from_json(nlohmann::json::array()));
  auto bad = nlohmann::json::object();
  bad["calendar"] = "martian";
  REQUIRE_FALSE(GenealogyDate::from_json(bad));
}

TEST_CASE("GenealogyDate：display", "[date]") {
  auto d = GenealogyDate::from_iso("1864-05-09");
  d->era = Era{"同治", 3};
  d->lunar = "四月初四";
  REQUIRE(d->display() == "同治三年 · 四月初四 · 甲子 · 1864-05-09");

  const GenealogyDate empty;
  REQUIRE(empty.is_empty());
  REQUIRE(empty.display() == "不详");
}

TEST_CASE("GenealogyDate：老谱原文自动认年号，并可由年号推出年份", "[date]") {
  const auto d = GenealogyDate::from_raw("同治三年四月初四");
  REQUIRE(d);
  REQUIRE(d->calendar == Calendar::Lunar);
  REQUIRE(d->lunar.value() == "同治三年四月初四");   // 原文照录不动
  REQUIRE(d->era.has_value());
  REQUIRE(d->era->name == "同治");
  REQUIRE(d->era->year == 3);
  REQUIRE(d->era_year().value() == 1864);
  REQUIRE(d->resolved_year().value() == 1864);

  // 繁体写法同样认得
  const auto t = GenealogyDate::from_raw("萬曆十五年");
  REQUIRE(t);
  REQUIRE(t->era.has_value());
  REQUIRE(t->era->name == "万历");
  REQUIRE(t->era->year == 15);
  REQUIRE(t->era_year().value() == 1587);

  // 「元年」也是常规写法
  const auto first = GenealogyDate::from_raw("康熙元年");
  REQUIRE(first);
  REQUIRE(first->era.has_value());
  REQUIRE(first->era->year == 1);
  REQUIRE(first->era_year().value() == 1662);

  // 同名年号（明「天顺」与元「天顺」）能认出年号，但换算会冲突——不猜
  const auto amb = GenealogyDate::from_raw("天顺元年");
  REQUIRE(amb);
  REQUIRE(amb->era.has_value());
  REQUIRE(amb->era->name == "天顺");
  REQUIRE_FALSE(amb->era_year().has_value());

  // 认不出年号的原文照旧，不猜测、不乱填
  const auto raw = GenealogyDate::from_raw("康熙年间");
  REQUIRE(raw);
  REQUIRE_FALSE(raw->era.has_value());
  REQUIRE_FALSE(raw->era_year().has_value());
  REQUIRE(raw->lunar.value() == "康熙年间");

  // 有公历信息时，resolved_year 以公历为准
  auto g = GenealogyDate::from_iso("1864-05-09");
  g->era = Era{"光绪", 1};
  REQUIRE(g->resolved_year().value() == 1864);
}
