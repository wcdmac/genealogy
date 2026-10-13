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
const truthPath = path.join(__dirname, '..', '..', '.data', 'pmo', 'pmo_truth.jsonl');
const truth = fs.existsSync(truthPath)
  ? fs.readFileSync(truthPath, 'utf8').trim().split('\n').map((s) => JSON.parse(s))
  : [];

const MON = ['', '正月', '二月', '三月', '四月', '五月', '六月', '七月',
  '八月', '九月', '十月', '十一月', '十二月'];
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
w('}  // namespace genealogy::calendar::fixtures');

const out = path.join(__dirname, '..', '..', 'core', 'tests', 'calendar_fixtures.hpp');
fs.writeFileSync(out, L.join('\n') + '\n');
console.log('写出:', out, fs.statSync(out).size, '字节');
console.log('月首:', monthRows.length, ' 节气:', terms.length, ' 干支抽样:', gzRows.length);
