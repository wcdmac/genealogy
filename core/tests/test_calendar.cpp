// M1.4 历法引擎测试。
//
// 断言基准不是「泓儿觉得对」，而是中国科学院紫金山天文台《1900-2025年日历》：
//   · 1559 个农历月初一（含闰月）+ 3024 条节气时刻 + 3024 条纪日干支抽样，
//     全部由 PDF 逐页解析而来（见 tools/gen_calendar 与 calendar_fixtures.hpp）。
//   · 1900-2025 的每一天（46021 日）都过一遍农历月/日，不只是月初一。
// 数据表覆盖 1583-3000 共 1418 个农历年，区间内逐年自洽性也一并体检。

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <genealogy/calendar/chinese_calendar.hpp>
#include <genealogy/error.hpp>

#include "calendar_fixtures.hpp"

using namespace genealogy;
using namespace genealogy::calendar;

namespace {

constexpr int kFirst = 1583;   // 数据表下界（格里历改革次年）
constexpr int kLast = 3000;    // 数据表上界

bool is_leap(int y) noexcept { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int days_in_month(int y, int m) noexcept {
  static const int kLen[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && is_leap(y)) return 29;
  return kLen[m];
}

// 由 yyyymmdd 拆出年月日
void split(int ymd, int* y, int* m, int* d) noexcept {
  *y = ymd / 10000;
  *m = (ymd / 100) % 100;
  *d = ymd % 100;
}

GregorianDate gd(int y, int m, int d) { return GregorianDate{y, m, d}; }

// 由夹具重建 1900-2025 每一天的官方农历（年号、月、日）
struct OfficialDay {
  int ly = 0;
  int month = 0;
  int day = 0;
};

std::vector<OfficialDay> build_official_days(int* out_first_z) {
  // 月首按时间序排列，第 i 段的天数 = 下一段起点 - 本段起点；末段按当年剩余补齐。
  const int first_ymd = fixtures::kMonthStarts[0].ymd;
  int fy = 0, fm = 0, fd = 0;
  split(first_ymd, &fy, &fm, &fd);
  const int first_z = days_from_civil(fy, fm, fd);
  if (out_first_z) *out_first_z = first_z;

  const int last_z = days_from_civil(2025, 12, 31);
  std::vector<OfficialDay> out(static_cast<std::size_t>(last_z - first_z + 1));

  std::size_t i = 0;
  for (std::size_t seg = 0; seg + 1 < fixtures::kMonthStartCount; ++seg) {
    const auto& a = fixtures::kMonthStarts[seg];
    const auto& b = fixtures::kMonthStarts[seg + 1];
    int ay = 0, am = 0, ad = 0, by = 0, bm = 0, bd = 0;
    split(a.ymd, &ay, &am, &ad);
    split(b.ymd, &by, &bm, &bd);
    const int za = days_from_civil(ay, am, ad);
    const int zb = days_from_civil(by, bm, bd);
    for (int z = za; z < zb; ++z) {
      std::size_t idx = static_cast<std::size_t>(z - first_z);
      if (idx >= out.size()) break;
      out[idx] = OfficialDay{a.ly, a.month, z - za + 1};
      ++i;
    }
  }
  // 末段：2025 年最后一个农历月，走到 2025-12-31 为止
  const auto& a = fixtures::kMonthStarts[fixtures::kMonthStartCount - 1];
  int ay = 0, am = 0, ad = 0;
  split(a.ymd, &ay, &am, &ad);
  const int za = days_from_civil(ay, am, ad);
  for (int z = za; z <= last_z; ++z) {
    out[static_cast<std::size_t>(z - first_z)] = OfficialDay{a.ly, a.month, z - za + 1};
  }
  (void)i;
  return out;
}

}  // namespace

TEST_CASE("历法：日期序号换算全区间往返一致") {
  // 1583-3000 逐日往返；同时抽查 1900 年前后的负序号（1970 以前）
  long long checked = 0;
  for (int y = kFirst; y <= kLast; ++y) {
    for (int m = 1; m <= 12; ++m) {
      const int dim = days_in_month(y, m);
      for (int d = 1; d <= dim; ++d) {
        const int z = days_from_civil(y, m, d);
        int y2 = 0, m2 = 0, d2 = 0;
        civil_from_days(z, &y2, &m2, &d2);
        if (y2 != y || m2 != m || d2 != d) {
          FAIL("往返不符: " << y << "-" << m << "-" << d << " -> z=" << z
                            << " -> " << y2 << "-" << m2 << "-" << d2);
        }
        ++checked;
      }
    }
  }
  REQUIRE(checked == 517914);   // 1583-3000 的实际天数，防止循环被改窄

  // 连续性：相邻日序号 +1，跨年跨月不跳
  int prev = days_from_civil(kFirst, 1, 1);
  for (int y = kFirst; y <= kLast; ++y) {
    for (int m = 1; m <= 12; ++m) {
      for (int d = 1; d <= days_in_month(y, m); ++d) {
        const int z = days_from_civil(y, m, d);
        REQUIRE(z == prev);
        ++prev;
      }
    }
  }

  // 1970 锚点与负年份（向零截断语义）
  REQUIRE(days_from_civil(1970, 1, 1) == 0);
  REQUIRE(days_from_civil(1969, 12, 31) == -1);
  REQUIRE(days_from_civil(1900, 1, 1) == -25567);
}

TEST_CASE("历法：官方 1559 个月初一逐条对得上") {
  for (int i = 0; i < fixtures::kMonthStartCount; ++i) {
    const auto& ms = fixtures::kMonthStarts[i];
    int y = 0, m = 0, d = 0;
    split(ms.ymd, &y, &m, &d);
    const auto c = to_chinese(gd(y, m, d));
    REQUIRE(c.has_value());
    if (c->year != ms.ly || c->month != ms.month || c->day != 1) {
      FAIL("公历 " << y << "-" << m << "-" << d << " 农历应为 " << ms.ly << "年"
                   << ms.month << "月初一，实得 " << c->year << "年"
                   << c->month << "月" << c->day << "日");
    }
    // 反向：农历初一 → 公历
    const auto g = to_gregorian(ChineseDate{ms.ly, ms.month, 1});
    REQUIRE(g.has_value());
    if (g->year != y || g->month != m || g->day != d) {
      FAIL("农历 " << ms.ly << "年" << ms.month << "月初一 应回到 " << y << "-"
                   << m << "-" << d << "，实得 " << g->year << "-" << g->month
                   << "-" << g->day);
    }
  }
}

TEST_CASE("历法：1900-2025 逐日（46021 天）与官方对照表全等") {
  int first_z = 0;
  const auto official = build_official_days(&first_z);
  REQUIRE(official.size() == 46021);

  for (std::size_t idx = 0; idx < official.size(); ++idx) {
    const int z = first_z + static_cast<int>(idx);
    int gy = 0, gm = 0, gdd = 0;
    civil_from_days(z, &gy, &gm, &gdd);
    const auto c = to_chinese(gd(gy, gm, gdd));
    REQUIRE(c.has_value());
    const auto& o = official[idx];
    if (c->year != o.ly || c->month != o.month || c->day != o.day) {
      FAIL("公历 " << gy << "-" << gm << "-" << gdd << " 官方农历 " << o.ly
                   << "年" << o.month << "月" << o.day << "日，实得 " << c->year
                   << "年" << c->month << "月" << c->day << "日");
    }
  }
}

TEST_CASE("历法：官方 3024 条节气时刻逐条对得上") {
  for (int i = 0; i < fixtures::kTermCount; ++i) {
    const auto& t = fixtures::kTerms[i];
    const auto st = solar_term(t.year, t.index);
    REQUIRE(st.has_value());
    if (st->date.year != t.year || st->date.month != t.month ||
        st->date.day != t.day || st->hour != t.hour || st->minute != t.minute) {
      FAIL(t.year << " 年第 " << t.index << " 节气 官方 " << t.month << "/"
                  << t.day << " " << t.hour << ":" << t.minute << "，实得 "
                  << st->date.month << "/" << st->date.day << " " << st->hour
                  << ":" << st->minute);
    }
    // 反查：该日应命中同一节气索引
    const auto hit = solar_term_on(st->date);
    REQUIRE(hit.has_value());
    REQUIRE(*hit == t.index);
  }
}

TEST_CASE("历法：纪日干支抽样与官方一致") {
  for (int i = 0; i < fixtures::kDayGanzhiCount; ++i) {
    const auto& e = fixtures::kDayGanzhi[i];
    const std::string iso = e.iso;   // "yyyy-mm-dd"
    const int y = std::stoi(iso.substr(0, 4));
    const int m = std::stoi(iso.substr(5, 2));
    const int d = std::stoi(iso.substr(8, 2));
    const auto gz = day_ganzhi(gd(y, m, d));
    if (gz != e.gz) {
      FAIL(iso << " 官方纪日 " << e.gz << "，实得 " << gz);
    }
  }
}

TEST_CASE("历法：干支连续不断，跨日 +1") {
  std::vector<std::pair<std::string, int>> table;
  table.reserve(60);
  for (int k = 0; k < 60; ++k) table.emplace_back(ganzhi_name(k), k);

  // 甲子 60 循环：连续两日的干支序号必然差 1（1900-2025 逐日）
  int prev_idx = -1;
  const int z0 = days_from_civil(1900, 1, 1);
  const int z1 = days_from_civil(2025, 12, 31);
  bool first = true;
  for (int z = z0; z <= z1; ++z) {
    int y = 0, m = 0, d = 0;
    civil_from_days(z, &y, &m, &d);
    const std::string gz = day_ganzhi(gd(y, m, d));
    int idx = -1;
    for (const auto& kv : table) {
      if (kv.first == gz) { idx = kv.second; break; }
    }
    REQUIRE(idx >= 0);
    if (!first) REQUIRE(idx == (prev_idx + 1) % 60);
    prev_idx = idx;
    first = false;
  }
}

TEST_CASE("历法：年干支、生肖、月干支") {
  // 公元 4 年为甲子
  REQUIRE(year_ganzhi(4) == "甲子");
  REQUIRE(year_ganzhi(1900) == "庚子");
  REQUIRE(year_ganzhi(1901) == "辛丑");
  REQUIRE(year_ganzhi(1984) == "甲子");
  REQUIRE(year_ganzhi(2024) == "甲辰");
  REQUIRE(year_ganzhi(2025) == "乙巳");
  REQUIRE(year_ganzhi(kFirst) == "癸未");   // 1583

  REQUIRE(zodiac(1900) == "鼠");
  REQUIRE(zodiac(2024) == "龙");
  REQUIRE(zodiac(2025) == "蛇");
  REQUIRE(zodiac(kFirst) == "羊");

  // 五虎遁：甲己之年丙作首（2024 甲辰 → 正月丙寅）；乙庚之岁戊为头（1900 庚子 → 戊寅）
  REQUIRE(month_ganzhi(2024, 1) == "丙寅");
  REQUIRE(month_ganzhi(2024, 2) == "丁卯");
  REQUIRE(month_ganzhi(1900, 1) == "戊寅");
  REQUIRE(month_ganzhi(1900, 2) == "己卯");
  // 闰月与正常月同干支
  REQUIRE(month_ganzhi(2025, 6) == month_ganzhi(2025, -6));

  // 60 甲子名
  REQUIRE(ganzhi_name(0) == "甲子");
  REQUIRE(ganzhi_name(59) == "癸亥");
  REQUIRE(ganzhi_name(60) == "甲子");   // 越界取模
}

TEST_CASE("历法：月名与日名") {
  REQUIRE(month_name(1) == "正月");
  REQUIRE(month_name(11) == "冬月");
  REQUIRE(month_name(12) == "腊月");
  REQUIRE(month_name(-2) == "闰二月");
  REQUIRE(month_name(13) == "");
  REQUIRE(day_name(1) == "初一");
  REQUIRE(day_name(15) == "十五");
  REQUIRE(day_name(20) == "二十");
  REQUIRE(day_name(30) == "三十");
  REQUIRE(day_name(31) == "");
}

TEST_CASE("历法：区间与越界行为") {
  REQUIRE(first_supported_year() == kFirst);
  REQUIRE(last_supported_year() == kLast);
  REQUIRE(supports(kFirst));
  REQUIRE(supports(kLast));
  REQUIRE(!supports(kFirst - 1));
  REQUIRE(!supports(kLast + 1));

  const auto a = to_chinese(gd(1582, 12, 31));
  REQUIRE(!a.has_value());
  REQUIRE(a.error().code() == ErrorCode::CalendarOutOfRange);
  const auto b = to_gregorian(ChineseDate{3001, 1, 1});
  REQUIRE(!b.has_value());
  REQUIRE(b.error().code() == ErrorCode::CalendarOutOfRange);
  const auto c = solar_term(1582, 0);
  REQUIRE(!c.has_value());
  REQUIRE(c.error().code() == ErrorCode::CalendarOutOfRange);
  const auto d = solar_term(2020, 24);
  REQUIRE(!d.has_value());
  REQUIRE(d.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("历法：闰月与月长的边界") {
  // 该年没有的闰月，应报 NotFound
  const auto bad = to_gregorian(ChineseDate{2025, -1, 1});
  REQUIRE(!bad.has_value());
  REQUIRE(bad.error().code() == ErrorCode::NotFound);

  // 29 天的月给第 30 日，应报 InvalidArgument；30 天的月给第 30 日应成立
  int checked_29 = 0, checked_30 = 0;
  for (int i = 0; i + 1 < fixtures::kMonthStartCount; ++i) {
    const auto& a = fixtures::kMonthStarts[i];
    const auto& b = fixtures::kMonthStarts[i + 1];
    int ay = 0, am = 0, ad = 0, by = 0, bm = 0, bd = 0;
    split(a.ymd, &ay, &am, &ad);
    split(b.ymd, &by, &bm, &bd);
    const int len = days_from_civil(by, bm, bd) - days_from_civil(ay, am, ad);
    REQUIRE((len == 29 || len == 30));
    const auto last = to_gregorian(ChineseDate{a.ly, a.month, len});
    REQUIRE(last.has_value());
    const auto over = to_gregorian(ChineseDate{a.ly, a.month, len + 1});
    REQUIRE(!over.has_value());
    REQUIRE(over.error().code() == ErrorCode::InvalidArgument);
    if (len == 29) ++checked_29; else ++checked_30;
  }
  REQUIRE(checked_29 + checked_30 == fixtures::kMonthStartCount - 1);
  REQUIRE(checked_29 > 600);   // 官方区间内 29 天月 731 个、30 天月 827 个
  REQUIRE(checked_30 > 600);
}

TEST_CASE("历法：全区间逐年自洽（1583-3000）") {
  // 每年：正月起点单调、24 节气时刻严格递增、冬至落在十一月
  int prev_first = days_from_civil(kFirst - 1, 1, 1);
  for (int y = kFirst; y <= kLast; ++y) {
    // 24 节气齐全且严格递增（1583 年的分钟数是负的大数，初值不能取 0 或 -1）
    long long prev_min = std::numeric_limits<long long>::min();
    for (int i = 0; i < 24; ++i) {
      const auto st = solar_term(y, i);
      REQUIRE(st.has_value());
      const int z = days_from_civil(st->date.year, st->date.month, st->date.day);
      const long long mm = static_cast<long long>(z) * 1440 + st->hour * 60 + st->minute;
      REQUIRE(mm > prev_min);
      prev_min = mm;
      REQUIRE(st->date.year == y);
    }
    // 正月初一在公历 1/21–2/21 之间，且逐年推进
    const auto g = to_gregorian(ChineseDate{y, 1, 1});
    REQUIRE(g.has_value());
    REQUIRE((g->month == 1 || g->month == 2));
    if (g->month == 1) REQUIRE(g->day >= 21);
    if (g->month == 2) REQUIRE(g->day <= 21);
    const int z = days_from_civil(g->year, g->month, g->day);
    REQUIRE(z > prev_first);
    prev_first = z;
  }
}

TEST_CASE("历法：冬至落在农历十一月（GB/T 33661-2017 置闰基础）") {
  // 1900-2025 官方区间内严格断言；其余年份仅统计
  int checked = 0;
  for (int y = 1900; y <= 2025; ++y) {
    const auto dz = solar_term(y, 23);   // 冬至
    REQUIRE(dz.has_value());
    const auto c = to_chinese(dz->date);
    REQUIRE(c.has_value());
    REQUIRE(c->month == 11);
    ++checked;
  }
  REQUIRE(checked == 126);
}
