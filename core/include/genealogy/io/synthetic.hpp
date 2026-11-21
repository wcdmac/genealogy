#pragma once

// 合成谱数据生成器（M1.6 性能验收用）。
//
// 为什么要「合成」而不是拿真谱：性能数字必须可复现、可对比、可进 CI，
// 真谱有隐私问题且每次跑的分布不一样。生成器用固定种子的线性同余法，
// 同一组参数永远长出同一份数据。
//
// 生成的家谱保持基本世系常理（否则十代树、时间悖论校验都测不出真实形态）：
//   · 每代人年龄递增，子女出生不早于父母 + 最小生育年龄；
//   · 婚姻都在生育之前，且配偶年龄相差不大；
//   · generation_no 逐代 +1，行几连续；
//   · 姓名从有限的姓/名池里取（同名同年是常见谱面现象，正好喂给 R5）。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>

namespace genealogy::io {

struct SyntheticOptions {
  std::size_t persons = 100000;   // 目标人物数（实际会有少量偏差）
  int generations = 14;           // 世代数（十代子孙树要 10 层以上）
  std::uint64_t seed = 20261013;  // 固定种子 → 可复现
  std::string lineage_id;         // 必填
  std::string surname = "陈";     // 主姓
  int founder_year = 1500;        // 始祖生年（合法域 40..2000，见 generate_synthetic）
  // 每人事件数（第三轮新-C8：bench 的 --events-per-person 此前注册了
  // 却从未接线）。基线 3 条 = 出生/卒年/葬期；>3 补「记事」事件，
  // 0 = 不生成事件。
  int events_per_person = 3;
};

struct SyntheticData {
  std::vector<Person> persons;
  std::vector<Union> unions;
  std::vector<Event> events;
  std::vector<Place> places;
  std::string founder_id;   // 始祖 id（十代树的起点）
};

// 生成一批自洽的合成谱数据。参数非法（lineage_id 空、founder_year/
// generations 越界——派生年会撞 from_iso 合法域）返回 InvalidArgument。
Result<SyntheticData> generate_synthetic(const SyntheticOptions& opt);

}  // namespace genealogy::io
