#pragma once

// 年号纪年（如「同治三年」）与公历/农历年的互转。
//
// 与历法引擎的分工：
//   · 年号 → 年：本模块（区间表，粒度为「年」）；
//   · 年 → 具体日期：历法引擎（chinese_calendar.hpp）。
// 合起来即可把「同治三年四月初四」解到 1864-05-09 这样的公历日。
//
// 注意：年号元年按「正月所在公历年」记，故农历年的冬月、腊月会落到
// 公历次年——这正是历法引擎要处理的事，年号层不做近似。

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>

namespace genealogy::calendar {

struct EraInfo {
  std::string dynasty;             // 简体朝代名
  std::string reign;               // 简体年号
  std::string reign_traditional;   // 繁体年号
  std::string note;                // 重名区分（可空）
  int start_year = 0;              // 元年
  int end_year = 0;                // 末年（含）
  bool mainline = false;           // 正统主线

  // 该年号第 n 年对应的（农历）年号
  int year_of(int n) const noexcept { return start_year + n - 1; }
  bool covers(int year) const noexcept {
    return year >= start_year && year <= end_year;
  }
};

// 年 → 年号：正统主线（缺省展示用）。无年号（如 1950 年后）返回 nullopt。
std::optional<EraInfo> era_of(int year);
// 年 → 年号：全部政权（含辽、金、西夏等并存政权）
std::vector<EraInfo> eras_of(int year);
// 按年号名查（简体或繁体均可），返回同名但不同朝代/区间的全部条目
std::vector<EraInfo> find_era(const std::string& reign);

// 年号纪年 → 年。同名年号（如元「至元」两见）若目标年只落在其中一段，
// 取该段；若落在多段则返回 Conflict，并在消息里列出候选。
Result<int> era_to_year(const std::string& reign, int year_in_era);
// 同上，但限定朝代——用于同名年号（如明「天顺」与元「天顺」）的精确换算。
Result<int> era_to_year(const std::string& dynasty, const std::string& reign,
                        int year_in_era);

// 中文数字（1..99）：元年 / 二年 / … / 十年 / … / 六十年
std::string chinese_number(int n);
// 「同治三年」这样的展示串
std::string era_display(const EraInfo& era, int year_in_era);

}  // namespace genealogy::calendar
