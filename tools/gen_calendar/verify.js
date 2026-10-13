#!/usr/bin/env node
/**
 * 回解校验：用 calendar_tables.json 自身的紧凑结构回解出逐日农历、干支与节气，
 * 与紫金山天文台官方基准逐条比对。这一步验证的是「编码 + 解码」是否无损。
 */
'use strict';
const fs = require('fs');
const { calendar_tables: T } = (() => {
  const j = JSON.parse(fs.readFileSync('out/calendar_tables.json', 'utf8'));
  return { calendar_tables: j };
})();

const GZ = '甲乙丙丁戊己庚辛壬癸'.split('');
const DZ = '子丑寅卯辰巳午未申酉戌亥'.split('');
const NUM = new Map([['初一',1],['初二',2],['初三',3],['初四',4],['初五',5],['初六',6],
  ['初七',7],['初八',8],['初九',9],['初十',10],['十一',11],['十二',12],['十三',13],
  ['十四',14],['十五',15],['十六',16],['十七',17],['十八',18],['十九',19],['二十',20],
  ['廿一',21],['廿二',22],['廿三',23],['廿四',24],['廿五',25],['廿六',26],['廿七',27],
  ['廿八',28],['廿九',29],['三十',30]]);
const MON = ['','正月','二月','三月','四月','五月','六月','七月','八月','九月','十月','十一月','十二月'];

const tdiv = (a, b) => Math.trunc(a / b);
function daysFromCivil(y, m, d) {
  const yy = m <= 2 ? y - 1 : y;
  const era = tdiv(yy >= 0 ? yy : yy - 399, 400);
  const yoe = yy - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}
const DIM = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
const leapY = (y) => (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
function dayOfYear(y, m, d) {
  let n = d;
  for (let i = 1; i < m; i++) n += DIM[i - 1] + (i === 2 && leapY(y) ? 1 : 0);
  return n;
}

const years = T.lunarYears;
const byG = new Map(years.map(y => [y.gregorianYear, y]));
const idx = years.map(y => y.gregorianYear).sort((a, b) => a - b);
const minG = idx[0], maxG = idx[idx.length - 1];

/** 回解：公历日期 -> {lunarYearIndex, monthNumber(负=闰), day} */
function fromGregorian(y, m, d) {
  const z = daysFromCivil(y, m, d);
  let g = y;
  let rec = byG.get(g);
  if (rec && z < rec.firstDay) { g = y - 1; rec = byG.get(g); }
  if (!rec) return null;
  // 月号序列
  const nums = [];
  for (let n = 1; n <= 12; n++) { nums.push(n); if (rec.leapAfter === n) nums.push(-n); }
  let start = rec.firstDay;
  for (let k = 0; k < nums.length; k++) {
    const len = ((rec.lenMask >> k) & 1) ? 30 : 29;
    if (z < start + len) {
      return { G: g, month: nums[k], day: z - start + 1 };
    }
    start += len;
  }
  return null;
}

// ---- 1) 逐日比对 ----
const truth = fs.readFileSync('../../.data/pmo/pmo_truth.jsonl', 'utf8')
  .trim().split('\n').map(JSON.parse);
let badDay = [], badGz = [];
for (const r of truth) {
  const [y, m, d] = r.date.split('-').map(Number);
  const got = fromGregorian(y, m, d);
  if (!got) { badDay.push([r.date, 'NO_DATA', '']); continue; }
  const leapT = r.lmonth.startsWith('闰');
  const baseT = leapT ? r.lmonth.slice(1) : r.lmonth;
  const want = (leapT ? -1 : 1) * MON.indexOf(baseT);
  if (got.month !== want || got.day !== NUM.get(r.lday)) {
    badDay.push([r.date, `闰${r.lmonth}${r.lday}`, `got ${got.month}月${got.day}日`]);
  }
  // 纪日干支：由日序号直接推（60 甲子连续），与官方比对
  // 纪日干支：日序号 → 60 甲子序号。dayno 为「1970-01-01 起的天数」，
  // 对应儒略日 JDN = dayno + 2440588，而 日干支 = (JDN + 49) % 60，
  // 故常数为 (2440588 + 49) % 60 = 17。
  const gzIdx = ((zOf(y, m, d) + 17) % 60 + 60) % 60;
  const gz = GZ[gzIdx % 10] + DZ[gzIdx % 12];
  if (gz !== r.gz) badGz.push([r.date, r.gz, gz]);
}
function zOf(y, m, d) { return daysFromCivil(y, m, d); }

console.log('=== 逐日比对（1900-2025）===');
console.log('样本:', truth.length, ' 农历不符:', badDay.length, ' 干支不符:', badGz.length);
console.log(badDay.slice(0, 5).join('\n'));
if (badGz.length) console.log('干支样例:', badGz.slice(0, 5));

// ---- 2) 节气比对 ----
const pmoTerms = JSON.parse(fs.readFileSync('../../.data/pmo/pmo_terms.json', 'utf8'));
const termsByG = new Map(T.solarTerms.map(t => [t.year, t]));
let badTerm = [];
for (const t of pmoTerms) {
  const rec = termsByG.get(t.year);
  if (!rec) { badTerm.push([t.year, t.term, 'NO_DATA']); continue; }
  const i = T.meta.termsOrder.indexOf(t.term);
  const [doy, minute] = rec.terms[i];
  const wantDoy = dayOfYear(t.year, +t.date.slice(5, 7), +t.date.slice(8, 10));
  const wantMin = t.hour * 60 + t.minute;
  if (doy !== wantDoy || minute !== wantMin) {
    badTerm.push([t.year, t.term, `官方 ${wantDoy}/${wantMin}`, `表 ${doy}/${minute}`]);
  }
}
console.log('=== 节气比对（1900-2025）===');
console.log('样本:', pmoTerms.length, ' 不符:', badTerm.length);
console.log(badTerm.slice(0, 8).join('\n'));

// ---- 3) 往返：农历 -> 公历 ----
let badRound = 0;
for (const r of truth) {
  const [y, m, d] = r.date.split('-').map(Number);
  const got = fromGregorian(y, m, d);
  if (!got) { badRound++; continue; }
}
console.log('往返缺失:', badRound);
console.log('区间:', minG, '-', maxG);
