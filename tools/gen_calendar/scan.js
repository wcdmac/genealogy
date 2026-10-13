#!/usr/bin/env node
/**
 * 全区间扫描：按「正向换算」枚举每月初一，同时体检每个农历年是否结构完整。
 * 用于确定可安全交付的年份下界——某些万年历库在古代年份会出现缺月等缺陷。
 */
'use strict';
const fs = require('fs');
const { Solar } = require('lunar-javascript');

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
const DIM = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
const leapY = (y) => (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;

const MIN = +(process.argv[2] || 1);
const MAX = +(process.argv[3] || 3000);

const starts = [];   // { dayno, L, month }
const problems = [];

for (let y = MIN; y <= MAX + 1; y++) {
  for (let m = 1; m <= 12; m++) {
    const dim = DIM[m - 1] + (m === 2 && leapY(y) ? 1 : 0);
    for (let d = 1; d <= dim; d++) {
      let l;
      try { l = Solar.fromYmd(y, m, d).getLunar(); }
      catch (e) { problems.push([y, m, d, 'THROW ' + e.message]); continue; }
      if (l.getDay() === 1) {
        starts.push({ dayno: daysFromCivil(y, m, d), L: l.getYear(), month: l.getMonth() });
      }
    }
  }
}

// 按「正月」切分农历年
const years = [];
let cur = null;
for (const s of starts) {
  if (s.month === 1) {
    if (cur) years.push(cur);
    cur = { L: s.L, months: [s] };
  } else if (cur) {
    cur.months.push(s);
  }
}
if (cur) years.push(cur);

// 体检：月数须为 12 或 13；月号须构成 1..12（闰月为重复号且紧跟其后）；月长 29/30
const anomalies = [];
for (const yr of years) {
  const ms = yr.months;
  const nums = ms.map(x => x.month);
  const bad = [];
  if (ms.length !== 12 && ms.length !== 13) bad.push('月数=' + ms.length);
  // 检查编号序列
  let expect = 1, seen = [];
  for (const n of nums) {
    if (n === expect) { expect++; }
    else if (n === -(expect - 1)) { /* 闰月：重复上月号 */ }
    else { bad.push('序号跳变 ' + n + '（期望 ' + expect + '）'); expect = Math.abs(n) + 1; }
    seen.push(n);
  }
  if (expect !== 13 && ms.length === 12) bad.push('未收于十二月');
  // 月长
  for (let i = 0; i + 1 < ms.length; i++) {
    const len = ms[i + 1].dayno - ms[i].dayno;
    if (len !== 29 && len !== 30) bad.push(`第${i + 1}月长=${len}`);
  }
  if (bad.length) {
    anomalies.push({ L: yr.L, count: ms.length, nums, issues: bad,
                     from: civilFromDays(ms[0].dayno).join('-') });
  }
}

console.log('扫描范围:', MIN, '-', MAX);
console.log('月首记录数:', starts.length, ' 农历年数:', years.length);
console.log('调用异常:', problems.length);
console.log('结构异常的农历年:', anomalies.length);
const byYear = anomalies.map(a => a.L);
if (anomalies.length) {
  const lo = Math.min(...byYear), hi = Math.max(...byYear);
  console.log('异常年号范围:', lo, '-', hi);
  console.log('异常年号（前 30）:', anomalies.slice(0, 30).map(a => a.L).join(','));
  console.log('样例详情:', JSON.stringify(anomalies.slice(0, 3), null, 1));
}
fs.writeFileSync('scan_problems.json', JSON.stringify({ problems, anomalies }, null, 1));
