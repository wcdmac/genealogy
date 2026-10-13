#pragma once

// 农历（夏历）历法引擎。
//
// 核心事实口径（与数据生成脚本一致）：
//   · 月首 = 定朔（日月视黄经相等）当日，北京时间 UTC+8；
//   · 十一月含冬至，无中气之月为闰月（GB/T 33661-2017）；
//   · 干支纪日连续不断，日序号 → 60 甲子 的换算常数已与官方表逐日核对。
//
// 区间：first_supported_year() .. last_supported_year()（生成数据表覆盖范围）。
// 区间外一律返回 CalendarOutOfRange，不做外推——宁可说「不支持」，也不给不确定的数。

#include <array>
#include <optional>
#include <string>

#include <genealogy/error.hpp>

namespace genealogy::calendar {

// 日序号：1970-01-01 = 0，向前为负。
int days_from_civil(int y, int m, int d) noexcept;
void civil_from_days(int z, int* y, int* m, int* d) noexcept;

struct GregorianDate {
  int year = 1;
  int month = 1;
  int day = 1;
};

struct ChineseDate {
  int year = 0;   // 农历年（与「正月所在的公历年」同号）
  int month = 1;  // 1..12；闰月记负，如 -2 表示闰二月
  int day = 1;    // 1..30

  bool is_leap_month() const noexcept { return month < 0; }
  int month_abs() const noexcept { return month < 0 ? -month : month; }
};

struct SolarTermMoment {
  int index = 0;       // 0..23，0 = 小寒
  std::string name;    // 如「立春」
  GregorianDate date;  // 北京时间
  int hour = 0;
  int minute = 0;
};

int first_supported_year() noexcept;
int last_supported_year() noexcept;
bool supports(int gregorian_year) noexcept;

// 公历 → 农历
Result<ChineseDate> to_chinese(const GregorianDate& date);
// 农历 → 公历（闰月须以负数给出；日期超出该月实际长度返回 InvalidArgument）
Result<GregorianDate> to_gregorian(const ChineseDate& date);

// 名称
std::string month_name(int month);   // 正月…腊月；闰月带「闰」前缀
std::string day_name(int day);       // 初一…三十
std::string ganzhi_name(int index);  // 0..59 → 甲子…癸亥

// 干支与生肖
std::string year_ganzhi(int lunar_year);
std::string month_ganzhi(int lunar_year, int month);
std::string day_ganzhi(const GregorianDate& date);
std::string zodiac(int lunar_year);

// 节气
Result<SolarTermMoment> solar_term(int year, int index);
// 该日是否为节气，返回索引（0..23）；否则 nullopt
std::optional<int> solar_term_on(const GregorianDate& date);

}  // namespace genealogy::calendar
