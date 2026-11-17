#pragma once

// 农历数据表（由 tools/gen_calendar 生成，勿手工编辑）。
// 覆盖区间见 first_year()/last_year()；数据溯源与核验见生成脚本说明。

#include <cstddef>
#include <cstdint>

namespace genealogy::calendar {

// 单个农历年的紧凑记录。
//   first_day   正月初一的日序号（days since 1970-01-01）
//   month_count 月数（12 或 13）
//   leap_after  0 = 无闰月；n = 第 n 月之后插入「闰 n 月」
//   len_mask    bit i 置位表示时间序第 i 个月为 30 天，否则 29 天
struct LunarYearRecord {
  int first_day;
  int month_count;
  int leap_after;
  unsigned len_mask;
};

// 24 节气名（小寒起、冬至止）
extern const char* const kTermNames[24];

const LunarYearRecord* lunar_years() noexcept;
std::size_t lunar_year_count() noexcept;
const std::uint16_t* term_day_of_year() noexcept;
const std::uint16_t* term_minute_of_day() noexcept;
int first_year() noexcept;
int last_year() noexcept;
// 节气表起始年（与 first_year() 独立：明代段无节气数据，自 1583 年起提供）
int term_first_year() noexcept;
const char* const* term_names() noexcept;

}  // namespace genealogy::calendar
