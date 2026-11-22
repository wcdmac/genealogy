#!/usr/bin/env node
/**
 * 由 calendar_tables.json 生成 C++ 静态数据表（core/src/calendar/calendar_tables.cpp）。
 * 数据为「生成产物」，随生成脚本一同入库，避免运行时解析 JSON。
 */
'use strict';
const fs = require('fs');
const path = require('path');

const json = JSON.parse(fs.readFileSync('out/calendar_tables.json', 'utf8'));
const years = json.lunarYears;
const terms = json.solarTerms;
const minG = Math.min(...years.map(y => y.gregorianYear));
const maxG = Math.max(...years.map(y => y.gregorianYear));

const L = [];
const w = (s) => L.push(s);

w('// 本文件由 tools/gen_calendar/gen.js + emit_cpp.js 自动生成，请勿手工编辑。');
w('//');
w('// 数据溯源：中国科学院紫金山天文台《1900-2025年日历》（逐日农历/干支、节气时刻）；');
w('//           1900-2025 之外由 lunar-javascript（MIT）按同一天文模型推算；');
w('//           ' + minG + '-' + json.meta.mingSegment.range[1] +
  ' 为大统历朔闰史实（历法事实数据，固化于 fixtures/ming_datong*_facts.json：');
w('//           1368-1582 儒略历史实口径、1583-1644 格里历口径，与明代老谱一致）。');
w('// 核验：1900-2025 逐日 46021/46021 一致，节气 3024/3024 一致（北京时间，到分）；');
w('//       大统历史实段与天文推算段在 1645 正月朔上无缝衔接。');
w('// 覆盖区间：' + minG + ' - ' + maxG + ' 年（含首尾）；节气自 ' +
  terms[0].year + ' 年起（大统历段无节气数据）。');
w('');
w('#include <genealogy/calendar/calendar_tables.hpp>');
w('');
w('namespace genealogy::calendar {');
w('');
w('namespace {');
w('');
w('// 农历年记录：以「正月所在的公历年」为索引（' + minG + ' 年起连续排列）。');
w('//   first_day  正月初一，日序号 = days since 1970-01-01');
w('//   month_count  月数（12 或 13）');
w('//   leap_after   0 = 无闰月；n = 第 n 月之后插入闰 n 月');
w('//   len_mask     bit i 置位表示时间序第 i 个月为 30 天（否则 29 天）');
w('const LunarYearRecord kLunarYears[] = {');
for (const y of years) {
  w(`  {${y.firstDay}, ${y.monthCount}, ${y.leapAfter}, ${y.lenMask}},  // ${y.gregorianYear}`);
}
w('};');
w('');
w('// 节气：每年 24 项，顺序同 kTermNames；doy = 年内第几天（1 起），');
w('// minute = 当日 00:00 起的分钟数（北京时间 UTC+8）。');
w('const std::uint16_t kTermDayOfYear[] = {');
for (const t of terms) {
  const row = t.terms.map(x => x[0]).join(', ');
  w('  ' + row + ',  // ' + t.year);
}
w('};');
w('');
w('const std::uint16_t kTermMinuteOfDay[] = {');
for (const t of terms) {
  const row = t.terms.map(x => x[1]).join(', ');
  w('  ' + row + ',  // ' + t.year);
}
w('};');
w('');
w('}  // namespace');
w('');
w('const char* const kTermNames[24] = {');
w('    ' + json.meta.termsOrder.map(t => '"' + t + '"').join(', ') + '};');
w('');
w('');
w('const LunarYearRecord* lunar_years() noexcept { return kLunarYears; }');
w('std::size_t lunar_year_count() noexcept { return ' + years.length + '; }');
w('const std::uint16_t* term_day_of_year() noexcept { return kTermDayOfYear; }');
w('const std::uint16_t* term_minute_of_day() noexcept { return kTermMinuteOfDay; }');
w('int first_year() noexcept { return ' + minG + '; }');
w('int last_year() noexcept { return ' + maxG + '; }');
w('int term_first_year() noexcept { return ' + terms[0].year + '; }');
w('const char* const* term_names() noexcept { return kTermNames; }');
w('');
w('}  // namespace genealogy::calendar');

const outDir = path.join(__dirname, '..', '..', 'core', 'src', 'calendar');
fs.mkdirSync(outDir, { recursive: true });
const out = path.join(outDir, 'calendar_tables.cpp');
fs.writeFileSync(out, L.join('\n') + '\n');
console.log('写出:', out, fs.statSync(out).size, '字节');
console.log('农历年记录:', years.length, ' 节气条目:', terms.length * 24);
