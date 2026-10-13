#!/usr/bin/env node
/**
 * 年号表离线抽取（一次性工具，产物入仓、本脚本本身不参与构建）。
 *
 * 数据来源：cn-era 0.4.1（MIT，含 CBDB 朝代编号），枚举 -140..2024 每一年的
 * 全部年号，反推年号区间；再用 opencc-js 做繁转简，得到简繁双名。
 *
 * 人工订正（与《中国历史纪年表》口径对齐）：
 *   1. 宣统止于 1911（1912-02-12 退位，1912 为民国元年）；
 *   2. 民国纪年止于 1949（大陆口径），其后用公元纪年。
 *
 * 产出：fixtures/era_intervals.json —— 由 emit_era.js 编译成 C++ 静态表。
 */
'use strict';
const fs = require('fs');
const path = require('path');

const DATA = path.join(__dirname, '..', '..', '.data', 'era');
const { convertYear } = require(path.join(DATA, 'package', 'dist', 'index.cjs'));
const OpenCC = require(path.join(DATA, 'occ', 'package', 'dist', 'umd', 'full.js'));
const t2cn = OpenCC.Converter({ from: 't', to: 'cn' });

const FROM = -140;
const TO = 2024;

// 枚举每一年的全部年号，按 (朝代, 年号) 归组
const groups = new Map();   // key: dynasty|reign → { years: Map<greg, eraYear> }
const mainlinePairs = new Set();

for (let y = FROM; y <= TO; ++y) {
  let all = [];
  try {
    all = convertYear(y, { mode: 'all' });
  } catch (e) {
    continue;
  }
  let main = [];
  try {
    main = convertYear(y);   // 默认 mainline
  } catch (e) { /* 忽略 */ }
  for (const m of main) mainlinePairs.add(`${m.dynasty_name}|${m.reign_title}`);

  for (const r of all) {
    const key = `${r.dynasty_name}|${r.reign_title}`;
    if (!groups.has(key)) groups.set(key, { dynasty: r.dynasty_name, reign: r.reign_title, years: new Map() });
    groups.get(key).years.set(y, r.year);
  }
}

// 同朝代内可能有重名年号（如唐「上元」674-676 与 760-761、北魏「永兴」两见），
// 归组时会被并成一条而出现年份断档，故按「连续且年序 +1」切成多条区间。
const rows = [];
for (const g of groups.values()) {
  const gregs = [...g.years.keys()].sort((a, b) => a - b);
  let runStart = 0;
  for (let i = 0; i <= gregs.length; ++i) {
    const broken = i === gregs.length ||
      (i > runStart && (gregs[i] !== gregs[i - 1] + 1 ||
                        g.years.get(gregs[i]) !== g.years.get(gregs[i - 1]) + 1));
    if (!broken) continue;
    const run = gregs.slice(runStart, i);
    runStart = i;
    const start = run[0] - (g.years.get(run[0]) - 1);
    let maxYear = 0;
    for (const y of run) maxYear = Math.max(maxYear, g.years.get(y));
    // 重名年号带区分注记（如「至元 (世祖)」），拆成 reign + note
    const cn = t2cn(g.reign);
    const m = cn.match(/^(.+?)\s*[（(](.+?)[）)]$/);
    rows.push({
      dynasty_t: g.dynasty,
      reign_t: g.reign,
      dynasty: t2cn(g.dynasty),
      reign: m ? m[1] : cn,
      note: m ? m[2] : '',
      start_year: start,
      end_year: start + maxYear - 1,
      mainline: mainlinePairs.has(`${g.dynasty}|${g.reign}`),
      gaps: 0,
    });
  }
}

// 人工订正
for (const r of rows) {
  if (r.dynasty === '清' && r.reign === '宣统' && r.end_year > 1911) r.end_year = 1911;
  if (r.reign === '民国' && r.end_year > 1949) r.end_year = 1949;
}

rows.sort((a, b) => a.start_year - b.start_year || a.dynasty.localeCompare(b.dynasty));

const bad = rows.filter((r) => r.gaps > 0);
console.log('年号条数:', rows.length);
console.log('区间异常条数:', bad.length);
for (const b of bad.slice(0, 20)) {
  console.log('  异常:', b.dynasty, b.reign, b.start_year, '-', b.end_year, 'gaps=' + b.gaps);
}

// 覆盖检查：1583-1949 是否每年都有主线年号
const mainRows = rows.filter((r) => r.mainline);
let missing = 0;
for (let y = 1583; y <= 1949; ++y) {
  if (!mainRows.some((r) => y >= r.start_year && y <= r.end_year)) {
    if (missing < 10) console.log('  主线缺失:', y);
    missing++;
  }
}
console.log('1583-1949 主线缺失年数:', missing);

const out = path.join(__dirname, 'fixtures', 'era_intervals.json');
fs.writeFileSync(out, JSON.stringify(rows, null, 1) + '\n', 'utf8');
console.log('写出:', out, fs.statSync(out).size, '字节');
console.log('样例:', JSON.stringify(rows.filter((r) => r.dynasty === '清').map((r) => [r.reign, r.start_year, r.end_year])));
