// extract_datong.js —— M9.2：从 ytliu0/ChineseCalendar 的朔闰数据
// (fixtures/datong_calendarData.js) 提取 1368-1582 段（儒略历史实口径），
// 输出 out/datong_years.json 供 gen.js 合并。
//
// 数据来源：ytliu0/ChineseCalendar（GPL-3.0）src/calendarData.js——
// 大统历定朔推步 + 明实录历书扫描件订正。提取的是历法事实（各月初一
// 日期），事实不受版权约束；来源已注明。
//
// 行格式（ytliu0 解码）：
//   [year, 各月初一 doy ×12, 闰月初一 doy(0=无), 闰的月序, 农历年天数]
// doy = 从当年 1 月 1 日起算的物理日序，按「当时行使的民用历」计：
//   ≤1582 儒略历、≥1583 格里历、1582 混合（10-04→10-15）。
// doy 可以超出当年长度（腊月初一落到次年 1 月，如 1368 年 doy=375），
// 同一公式依然成立——doy 只是「1 月 1 日起的物理日数」。
//
// doy → 绝对 dayno（proleptic-Gregorian days since 1970-01-01）：
//   dayno = days_from_civil_proleptic(y, 1, 1) + offset(y) + doy - 1
// offset(y) = floor(y/100) - floor(y/400) - 2（儒略历落后格里历的天数：
//   1368-1399=8、1400-1499=9、1500-1582=10）；儒略 doy 换格里要 +offset。
// 1582 混合年同样 +10 全年成立：儒略段 doy=民用儒略日序（+10 对），
// 格里段 civil-doy 278=Oct-15=proleptic-288（+10 也对）——由连续性校验
// 自证，无需逐日分支。
//
// 自证锚点：洪武元年正月初一 = 儒略 1368-01-20（正月初四即位 = 儒略
// 1368-01-23）→ 1368 first_day 必须落在 proleptic 1368-01-28。
"use strict";
const fs = require('fs');
const path = require('path');

// Howard Hinnant 的 days_from_civil（proleptic Gregorian）
function days_from_civil(y, m, d) {
  y -= m <= 2;
  const era = Math.floor(y / 400);
  const yoe = y - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}

// 儒略历相对格里历的落后天数（时代值，1582 年及以前民用历为儒略历）
function julian_offset(y) {
  return y <= 1582 ? Math.floor(y / 100) - Math.floor(y / 400) - 2 : 0;
}

// 当年 1 月 1 日那天的 offset：用上一年的时代值。儒略历世纪闰年
// （1400/1500 等有 2-29 而格里历没有）的落后天数要到 3 月 1 日才 +1，
// 所以 1400 年 1-2 月仍按 8 算、3 月起按 9 算——锚点取 y-1 的时代值。
// doy 是「从当年 1 月 1 日起的物理日数」，锚定后逐日计数不再涉及
// offset（2-29 之类只是日序标签）。
function anchor_offset(y) {
  return y <= 1582 ? julian_offset(y - 1) : 0;
}

function main() {
  const src = fs.readFileSync(
    path.join(__dirname, 'fixtures', 'datong_calendarData.js'), 'utf8');

  // 提取 ChineseToGregorian 的数组（唯一的 return [[...]] 大数组）
  const m = src.match(/return \[\[(.*?)\]\]/s);
  if (!m) throw new Error('datong_calendarData.js 中找不到数据数组');
  const rows = JSON.parse('[[' + m[1] + ']]');
  console.log('[datong] 总年数:', rows.length,
    '范围:', rows[0][0], '-', rows[rows.length - 1][0]);

  const LO = 1368, HI = 1582;
  const YEARS = [];
  for (const row of rows) {
    const y = row[0];
    if (y < LO || y > HI) continue;
    // 列：[0]=年 [1..12]=正月..十二月月初 doy [13]=闰月初一 doy(0=无)
    //     [14]=闰的月序 [15]=农历年天数（正月初一→次年正月初一）
    const leapDoy = row[13];
    const leapNum = row[14];
    const yearDays = row[15];
    if (leapDoy > 0 && (leapNum < 1 || leapNum > 12)) {
      throw new Error(y + ' 闰月序非法: ' + leapNum);
    }

    const base = days_from_civil(y, 1, 1) + anchor_offset(y);
    // 各月初一的绝对 dayno（含闰月首日，按时间序）
    const starts = [];
    for (let k = 1; k <= 12; ++k) {
      starts.push({ doy: row[k], dayno: base + row[k] - 1, leap: false });
    }
    if (leapDoy > 0) {
      starts.push({ doy: leapDoy, dayno: base + leapDoy - 1, leap: true });
    }
    starts.sort((a, b) => a.dayno - b.dayno);

    // 月长：下一月初一 - 本月初一；末月由农历年天数收尾
    // （年天数从正月初一起算：正月初一 + yearDays = 次年正月初一）
    const lengths = [];
    for (let i = 0; i < starts.length; ++i) {
      const nextStart = i + 1 < starts.length
        ? starts[i + 1].dayno
        : starts[0].dayno + yearDays;
      lengths.push(nextStart - starts[i].dayno);
    }
    for (const L of lengths) {
      if (L !== 29 && L !== 30) {
        throw new Error(y + ' 存在非法月长 ' + L + '（结构异常年，拒绝生成）');
      }
    }

    // leap_after：gen.js 格式 = 闰月前的月序（闰七月 → 7）
    const leapIdxInSeq = starts.findIndex(s => s.leap);
    const leapAfter = leapDoy > 0 ? leapIdxInSeq : 0;

    YEARS.push({
      year: y,
      first_day: starts[0].dayno,
      month_count: starts.length,
      leap_after: leapAfter,
      len_mask: lengths.reduce((acc, L, i) => acc | (L === 30 ? (1 << i) : 0), 0),
      source: 'datong',
    });
  }

  // 跨年连续性：本年正月初一 + 农历年天数 = 次年正月初一
  // （offset 处理错任何一年，这里必炸——是换算正确性的自证）
  for (let i = 0; i + 1 < YEARS.length; ++i) {
    const cur = YEARS[i], next = YEARS[i + 1];
    const expect = cur.first_day + rows.find(r => r[0] === cur.year)[15];
    if (expect !== next.first_day) {
      throw new Error('连续性断裂: ' + cur.year + '→' + next.year +
        ' (' + expect + ' vs ' + next.first_day + ')');
    }
  }

  // 锚点断言：洪武元年正月初一 = 儒略 1368-01-20 = proleptic 1368-01-28
  const y1368 = YEARS[0];
  const anchor = days_from_civil(1368, 1, 28);
  if (y1368.year !== 1368 || y1368.first_day !== anchor) {
    throw new Error('洪武元年锚点失配: first_day=' + y1368.first_day +
      ' 期望 ' + anchor);
  }
  // 1582→1583 边界（1583 行是格里历 doy，同公式 offset=0）
  const r1582 = rows.find(r => r[0] === 1582);
  const r1583 = rows.find(r => r[0] === 1583);
  const end1582 = days_from_civil(1582, 1, 1) + anchor_offset(1582) + r1582[1] - 1;
  const start1583 = days_from_civil(1583, 1, 1) + r1583[1] - 1;
  if (end1582 + r1582[15] !== start1583) {
    throw new Error('1582→1583 边界断裂 (' + (end1582 + r1582[15]) +
      ' vs ' + start1583 + ')');
  }

  const out = path.join(__dirname, 'out', 'datong_years.json');
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, JSON.stringify({
    source: 'ytliu0/ChineseCalendar (GPL-3.0) 大统历朔闰+明实录订正',
    calendar: 'julian-dated（儒略历史实口径，正月初一=儒略历日期）',
    years: YEARS,
  }, null, 1));
  console.log('[datong] 写出', out, YEARS.length, '年', LO, '-', HI);
  console.log('[datong] 锚点 ✓ 1368 first_day =', y1368.first_day,
    '(proleptic 1368-01-28 = 儒略 01-20)');
  console.log('[datong] 连续性 ✓ 1368→1582 段内无断裂；1582→1583 边界 ✓');
}

main();
