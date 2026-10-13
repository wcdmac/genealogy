#pragma once

// 年号区间表（由 tools/gen_calendar 生成，勿手工编辑）。
// 覆盖西汉建元（前 140）至民国（1949），共 499 条区间。
//
// 口径说明：
//   · start_year 记「该年号元年正月所在的公历年」，与农历年号同尺度，
//     故可直接接到历法引擎上做 年月日 级换算；
//   · 改元之年在标准纪年表里会出现两条年号重叠（如 1620 年既是万历四十八年
//     又是泰昌元年），这符合《中国历史纪年表》惯例，不做人为取舍；
//   · 年号与公历年的对应以「年」为粒度，古代年内改元的月份差不予表达。

#include <cstddef>

namespace genealogy::calendar {

struct EraRecord {
  const char* dynasty;    // 简体朝代名，如「清」
  const char* reign;      // 简体年号，如「同治」
  const char* reign_traditional;  // 繁体年号，如「同治」（同形时与简体一致）
  const char* note;       // 重名区分，如「世祖」；无则空串
  int start_year;         // 元年
  int end_year;           // 末年（含）
  bool mainline;          // 是否属正统主线（用于默认展示）
};

const EraRecord* era_records() noexcept;
std::size_t era_record_count() noexcept;

}  // namespace genealogy::calendar
