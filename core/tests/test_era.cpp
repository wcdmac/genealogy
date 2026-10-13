// M1.4 年号纪年测试。
// 断言基准是明清民国年号元年的人工锚点（era_fixtures.hpp，与数据表相互独立），
// 外加一条端到端链路：年号 → 农历年 → 具体公历日（历法引擎）。

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <string>
#include <vector>

#include <genealogy/calendar/chinese_calendar.hpp>
#include <genealogy/calendar/era.hpp>
#include <genealogy/error.hpp>

#include "era_fixtures.hpp"

using namespace genealogy;
using namespace genealogy::calendar;

namespace {

const EraInfo* find_exact(const std::vector<EraInfo>& v, const std::string& dynasty,
                          const std::string& reign) {
  for (const auto& e : v) {
    if (e.dynasty == dynasty && e.reign == reign) return &e;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("年号：明清民国元年锚点逐条对得上") {
  for (int i = 0; i < fixtures::kEraAnchorCount; ++i) {
    const auto& a = fixtures::kEraAnchors[i];
    const auto hits = find_era(a.reign);
    const EraInfo* e = find_exact(hits, a.dynasty, a.reign);
    REQUIRE(e != nullptr);
    if (e->start_year != a.first_year || e->end_year != a.last_year) {
      FAIL(a.dynasty << "「" << a.reign << "」锚点 " << a.first_year << "-"
                     << a.last_year << "，表内为 " << e->start_year << "-"
                     << e->end_year);
    }
    // 元年与末年双向可解（明「天顺」与元「天顺」重名，故带朝代限定）
    const auto y1 = era_to_year(a.dynasty, a.reign, 1);
    REQUIRE(y1.has_value());
    REQUIRE(*y1 == a.first_year);
    const int span = a.last_year - a.first_year + 1;
    const auto yn = era_to_year(a.dynasty, a.reign, span);
    REQUIRE(yn.has_value());
    REQUIRE(*yn == a.last_year);
    // 超出该年号长度应报 InvalidArgument
    const auto over = era_to_year(a.dynasty, a.reign, span + 1);
    REQUIRE(!over.has_value());
    REQUIRE(over.error().code() == ErrorCode::InvalidArgument);
  }
}

TEST_CASE("年号：1583-1949 每年都有正统年号，其后没有") {
  for (int y = 1583; y <= 1949; ++y) {
    const auto e = era_of(y);
    if (!e.has_value()) {
      FAIL(y << " 年无正统年号（数据表断档）");
    }
    REQUIRE(e->covers(y));
    REQUIRE(e->mainline);
  }
  REQUIRE(!era_of(1950).has_value());   // 其后用公元纪年
  REQUIRE(!era_of(3000).has_value());
  REQUIRE(era_of(1912)->reign == "民国");
  REQUIRE(era_of(1864)->reign == "同治");
  REQUIRE(era_of(1864)->year_of(3) == 1864);
}

TEST_CASE("年号：改元之年重叠，主线取后起年号") {
  // 1620 年：万历四十八年 / 泰昌元年 并用，通常说泰昌
  const auto main = era_of(1620);
  REQUIRE(main.has_value());
  REQUIRE(main->reign == "泰昌");
  const auto all = eras_of(1620);
  int wanli = 0, taichang = 0;
  for (const auto& e : all) {
    if (e.reign == "万历") ++wanli;
    if (e.reign == "泰昌") ++taichang;
  }
  REQUIRE(wanli == 1);
  REQUIRE(taichang == 1);
}

TEST_CASE("年号：重名年号按年份消歧，真冲突才报") {
  // 元「至元」两见：世祖 1264-1294（1..31 年）、顺帝 1335-1340（1..6 年）。
  // 第 1..6 年两边都落得下 → 真冲突；第 7..31 年只有世祖 → 唯一解。
  const auto z1 = era_to_year("至元", 1);
  REQUIRE(!z1.has_value());
  REQUIRE(z1.error().code() == ErrorCode::Conflict);
  const auto z7 = era_to_year("至元", 7);
  REQUIRE(z7.has_value());
  REQUIRE(*z7 == 1270);
  const auto z31 = era_to_year("至元", 31);
  REQUIRE(z31.has_value());
  REQUIRE(*z31 == 1294);
  const auto z32 = era_to_year("至元", 32);
  REQUIRE(!z32.has_value());
  REQUIRE(z32.error().code() == ErrorCode::InvalidArgument);
  // 明「天顺」1457 起、元「天顺」1328 起，元年有两个候选 → Conflict
  const auto ts = era_to_year("天顺", 1);
  REQUIRE(!ts.has_value());
  REQUIRE(ts.error().code() == ErrorCode::Conflict);
  // 加朝代限定即可精确定位：明「天顺」元年 = 1457，元「天顺」元年 = 1328
  const auto ming = era_to_year("明", "天顺", 1);
  REQUIRE(ming.has_value());
  REQUIRE(*ming == 1457);
  const auto yuan = era_to_year("元", "天顺", 1);
  REQUIRE(yuan.has_value());
  REQUIRE(*yuan == 1328);
  // 无此年号
  const auto none = era_to_year("不存在的年号", 1);
  REQUIRE(!none.has_value());
  REQUIRE(none.error().code() == ErrorCode::NotFound);
  // 年份非法
  const auto zero = era_to_year("康熙", 0);
  REQUIRE(!zero.has_value());
  REQUIRE(zero.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("年号：繁体字写法也能查到") {
  const auto t = find_era("萬曆");
  REQUIRE(!t.empty());
  bool hit = false;
  for (const auto& e : t) {
    if (e.dynasty == "明" && e.reign == "万历") hit = true;
  }
  REQUIRE(hit);
  const auto t2 = find_era("光緒");
  REQUIRE(!t2.empty());
  REQUIRE(t2.front().reign == "光绪");
}

TEST_CASE("年号：中文数字与展示串") {
  REQUIRE(chinese_number(1) == "一");
  REQUIRE(chinese_number(2) == "二");
  REQUIRE(chinese_number(10) == "十");
  REQUIRE(chinese_number(11) == "十一");
  REQUIRE(chinese_number(20) == "二十");
  REQUIRE(chinese_number(35) == "三十五");
  REQUIRE(chinese_number(60) == "六十");
  REQUIRE(chinese_number(61) == "六十一");
  const auto e = era_of(1864);
  REQUIRE(e.has_value());
  REQUIRE(era_display(*e, 3) == "同治三年");
}

TEST_CASE("年号 → 农历 → 公历：端到端") {
  // 「同治三年四月初四」应解到 1864-05-09（设计文档与 README 的示例）
  const auto year = era_to_year("同治", 3);
  REQUIRE(year.has_value());
  REQUIRE(*year == 1864);
  const auto g = to_gregorian(ChineseDate{*year, 4, 4});
  REQUIRE(g.has_value());
  REQUIRE(g->year == 1864);
  REQUIRE(g->month == 5);
  REQUIRE(g->day == 9);
  // 反向：公历 → 农历 → 年号
  const auto c = to_chinese(GregorianDate{1864, 5, 9});
  REQUIRE(c.has_value());
  REQUIRE(c->year == 1864);
  const auto back = era_of(c->year);
  REQUIRE(back.has_value());
  REQUIRE(era_display(*back, c->year - back->start_year + 1) == "同治三年");
  REQUIRE(year_ganzhi(c->year) == "甲子");   // 1864 年确为甲子
}

TEST_CASE("年号：冬月腊月会落到公历次年（年号层不做近似）") {
  // 光绪三十四年冬月初一 = 1908-11-24（已在次公历年，但仍是光绪三十四年）
  const auto g = to_gregorian(ChineseDate{1908, 11, 1});
  REQUIRE(g.has_value());
  REQUIRE(g->year == 1908);
  REQUIRE(g->month == 11);
  REQUIRE(g->day == 24);
  const auto e = era_of(1908);
  REQUIRE(e.has_value());
  REQUIRE(e->reign == "光绪");
  REQUIRE(era_display(*e, 1908 - e->start_year + 1) == "光绪三十四年");
}
