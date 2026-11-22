#!/usr/bin/env node
/**
 * 由紫金山天文台官方基准生成 C++ 测试夹具：
 *   · 1900-2025 全部 1559 个农历月初一（含闰月，带农历年号）
 *   · 1900-2025 全部 3024 条节气时刻（北京时间，到分）
 *   · 1900-2025 每月 1 日 / 15 日的纪日干支（官方逐日表抽样）
 * 这些是「官方说过的话」，用来卡住历法引擎，防止任何回归。
 */
'use strict';
const fs = require('fs');
const path = require('path');

const months = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'pmo_months.json'), 'utf8'));
const terms = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'pmo_terms.json'), 'utf8'));
// 第六轮 高-3：干支夹具必须来自入库 fixture（新 clone 也有），
// 不能依赖 gitignore 的 .data/pmo/pmo_truth.jsonl——缺失时静默降级
// 为空数组会让 3024 条回归断言消失而测试仍绿（假绿）。
const truthPath = path.join(__dirname, 'fixtures', 'pmo_ganzhi_samples.json');
if (!fs.existsSync(truthPath)) {
  throw new Error('缺少 fixtures/pmo_ganzhi_samples.json：干支校验样本未入库');
}
const truthFile = JSON.parse(fs.readFileSync(truthPath, 'utf8'));
if (!truthFile.samples || truthFile.samples.length === 0) {
  throw new Error('fixtures/pmo_ganzhi_samples.json 无样本，拒绝生成空夹具');
}
const truth = truthFile.samples;

const MON = ['', '正月', '二月', '三月', '四月', '五月', '六月', '七月',
  '八月', '九月', '十月', '十一月', '十二月'];

// ---- 大统历事实段（1368-1644）正月初一的民用历标签（≤1582 儒略、≥1583 格里）----
// 与引擎 julian_from_days 同规则：锚点 = proleptic(y,1,1) + 上一年的
// 儒略落后天数；doy 为物理日序。仅用于生成测试期望值。
const tdiv = (a, b) => Math.trunc(a / b);
function daysFromCivil(y, m, d) {
  const yy = m <= 2 ? y - 1 : y;
  const era = tdiv(yy >= 0 ? yy : yy - 399, 400);
  const yoe = yy - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}
function civilFromDays(z) {
  const zz = z + 719468;
  const era = tdiv(zz >= 0 ? zz : zz - 146096, 146097);
  const doe = zz - era * 146097;
  const yoe = Math.floor((doe - Math.floor(doe / 1460) +
    Math.floor(doe / 36524) - Math.floor(doe / 146096)) / 365);
  const y = yoe + era * 400;
  const doy = doe - (365 * yoe + Math.floor(yoe / 4) - Math.floor(yoe / 100));
  const mp = Math.floor((5 * doy + 2) / 153);
  const d = doy - Math.floor((153 * mp + 2) / 5) + 1;
  const m = mp + (mp < 10 ? 3 : -9);
  return [m <= 2 ? y + 1 : y, m, d];
}
const julOff = (y) => (y <= 1582 ? Math.floor(y / 100) - Math.floor(y / 400) - 2 : 0);
const julAnchor = (y) => daysFromCivil(y, 1, 1) + julOff(y - 1);
function julianFromDays(z) {
  const [yp] = civilFromDays(z);
  const yy = z >= julAnchor(yp) ? yp : yp - 1;
  let jdoy = z - julAnchor(yy) + 1;
  const DIM = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  const leap = yy % 4 === 0;   // 儒略历世纪年也闰
  let mo = 1;
  while (mo <= 12) {
    const dim = DIM[mo - 1] + (mo === 2 && leap ? 1 : 0);
    if (jdoy <= dim) break;
    jdoy -= dim;
    mo++;
  }
  return [yy, mo, jdoy];
}
// 大统历事实段（1368-1644）两份固化事实表
const datongYears = ['ming_datong_facts.json', 'ming_datong_gregorian_facts.json']
  .flatMap((f) => {
    const fp = path.join(__dirname, 'fixtures', f);
    return fs.existsSync(fp) ? JSON.parse(fs.readFileSync(fp, 'utf8')).years : [];
  });
const datongRows = datongYears.map((y) => {
  // 与引擎 to_gregorian 一致：1582-10-15 前用儒略标签，之后用格里标签
  const beforeReform = y.first_day < daysFromCivil(1582, 10, 15);
  const [jy, jm, jd] = beforeReform
    ? julianFromDays(y.first_day) : civilFromDays(y.first_day);
  const iso = `${jy}-${String(jm).padStart(2, '0')}-${String(jd).padStart(2, '0')}`;
  return { year: y.year, iso, monthCount: y.month_count,
           leapAfter: y.leap_after, lenMask: y.len_mask };
});
const TERMS = ['小寒','大寒','立春','雨水','惊蛰','春分','清明','谷雨',
  '立夏','小满','芒种','夏至','小暑','大暑','立秋','处暑',
  '白露','秋分','寒露','霜降','立冬','小雪','大雪','冬至'];

function toMonth(lm) {
  const leap = lm.startsWith('闰');
  const base = leap ? lm.slice(1) : lm;
  const n = MON.indexOf(base);
  if (n <= 0) throw new Error('未知月名 ' + lm);
  return leap ? -n : n;
}

// 农历年号：正月必落在与其同号的公历年（1/21–2/21 之间），
// 故沿时间序推进、遇正月即以该年公历年为农历年号；首条若非正月则属上一年。
const monthRows = [];
let curLYear = Number(months[0][0].slice(0, 4)) - 1;
for (const m of months) {
  const [y, mo, d] = m[0].split('-').map(Number);
  const mm = toMonth(m[1]);
  if (mm === 1) curLYear = y;
  monthRows.push({ ymd: y * 10000 + mo * 100 + d, ly: curLYear, month: mm });
}

// 纪日干支抽样：每月 1 日与 15 日
const gzRows = [];
if (truth.length) {
  const byDate = new Map(truth.map((r) => [r.date, r.gz]));
  for (let y = 1900; y <= 2025; ++y) {
    for (let m = 1; m <= 12; ++m) {
      for (const d of [1, 15]) {
        const iso = `${y}-${String(m).padStart(2, '0')}-${String(d).padStart(2, '0')}`;
        const gz = byDate.get(iso);
        if (gz) gzRows.push([iso, gz]);
      }
    }
  }
}

const L = [];
const w = (s) => L.push(s);

w('// 本文件由 tools/gen_calendar/emit_tests.js 自动生成，请勿手工编辑。');
w('// 数据来源：中国科学院紫金山天文台《1900-2025年日历》（PDF 解析）。');
w('// 用途：历法引擎的官方回归基准——凡与此不符者即为回归。');
w('');
w('#pragma once');
w('');
w('namespace genealogy::calendar::fixtures {');
w('');
w('// 农历月初一：ymd = yyyymmdd；ly = 农历年号；month 为负表示闰月');
w('struct MonthStart {');
w('  int ymd;');
w('  int ly;      // 农历年号');
w('  int month;   // 1..12，负 = 闰月');
w('  int day;     // 恒为 1');
w('};');
w('');
w('inline constexpr MonthStart kMonthStarts[] = {');
for (const r of monthRows) {
  w(`  {${r.ymd}, ${r.ly}, ${r.month}, 1},`);
}
w('};');
w(`inline constexpr int kMonthStartCount = ${monthRows.length};`);
w('');
w('// 节气时刻（北京时间）');
w('struct TermExpect {');
w('  int year;');
w('  int index;   // 0..23，0 = 小寒');
w('  int month;');
w('  int day;');
w('  int hour;');
w('  int minute;');
w('};');
w('');
w('inline constexpr TermExpect kTerms[] = {');
for (const t of terms) {
  const [, mo, d] = t.date.split('-').map(Number);
  const i = TERMS.indexOf(t.term);
  if (i < 0) throw new Error('未知节气 ' + t.term);
  w(`  {${t.year}, ${i}, ${mo}, ${d}, ${t.hour}, ${t.minute}},`);
}
w('};');
w(`inline constexpr int kTermCount = ${terms.length};`);
w('');
w('// 纪日干支抽样（官方逐日对照表，每月 1 日与 15 日）');
w('struct DayGanzhi {');
w('  const char* iso;');
w('  const char* gz;');
w('};');
w('');
w('inline constexpr DayGanzhi kDayGanzhi[] = {');
for (const [iso, gz] of gzRows) {
  w(`  {"${iso}", "${gz}"},`);
}
w('};');
w(`inline constexpr int kDayGanzhiCount = ${gzRows.length};`);
w('');
w('// 大统历事实段（1368-1644 朔闰史实，固化于 fixtures/ming_datong_{gregorian_}facts.json）：');
w('// 正月初一的儒略历标签（儒略历史实口径）与结构字段。');
w('struct DatongYear {');
w('  int year;          // 农历年号（= 正月初一所在儒略历年）');
w('  const char* iso;   // 正月初一，儒略历 yyyy-mm-dd');
w('  int monthCount;    // 月数（12 或 13）');
w('  int leapAfter;     // 0 = 无闰月；n = 第 n 月之后插闰 n 月');
w('  unsigned lenMask;  // bit i = 时间序第 i 月 30 天');
w('};');
w('');
w('inline constexpr DatongYear kDatongYears[] = {');
for (const r of datongRows) {
  w(`  {${r.year}, "${r.iso}", ${r.monthCount}, ${r.leapAfter}, ${r.lenMask}},`);
}
w('};');
w(`inline constexpr int kDatongYearCount = ${datongRows.length};`);
w('');
w('}  // namespace genealogy::calendar::fixtures');

const out = path.join(__dirname, '..', '..', 'core', 'tests', 'calendar_fixtures.hpp');
fs.writeFileSync(out, L.join('\n') + '\n');
console.log('写出:', out, fs.statSync(out).size, '字节');
console.log('月首:', monthRows.length, ' 节气:', terms.length, ' 干支抽样:', gzRows.length);
