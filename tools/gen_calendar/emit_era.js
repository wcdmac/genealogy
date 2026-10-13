#!/usr/bin/env node
/**
 * 年号区间表 → C++ 静态表。
 * 输入 fixtures/era_intervals.json（由 extract_era.js 生成并已入仓），
 * 输出 core/src/calendar/era_tables.cpp 与 core/tests/era_fixtures.hpp。
 */
'use strict';
const fs = require('fs');
const path = require('path');

const rows = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'era_intervals.json'), 'utf8'));
rows.sort((a, b) => a.start_year - b.start_year || a.dynasty.localeCompare(b.dynasty) ||
  a.reign.localeCompare(b.reign));

const q = (s) => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';

const L = [];
const w = (s) => L.push(s);

w('// 本文件由 tools/gen_calendar/emit_era.js 自动生成，请勿手工编辑。');
w('// 数据来源：cn-era 0.4.1（MIT，朝代编号取自 CBDB），繁转简由 opencc-js 完成；');
w('// 并按《中国历史纪年表》口径订正：宣统止于 1911，民国纪年止于 1949。');
w('// 年号元年记为「该年号元年正月所在的公历年」，与农历年号口径一致。');
w('');
w('#include <genealogy/calendar/era_tables.hpp>');
w('');
w('namespace genealogy::calendar {');
w('');
w('// clang-format off');
w('const EraRecord kEraRecords[] = {');
for (const r of rows) {
  w(`  {${q(r.dynasty)}, ${q(r.reign)}, ${q(r.reign_t)}, ${q(r.note)}, ` +
    `${r.start_year}, ${r.end_year}, ${r.mainline ? 'true' : 'false'}},`);
}
w('};');
w('// clang-format on');
w('');
w('const EraRecord* era_records() noexcept { return kEraRecords; }');
w(`std::size_t era_record_count() noexcept { return ${rows.length}; }`);
w('');
w('}  // namespace genealogy::calendar');

const cpp = path.join(__dirname, '..', '..', 'core', 'src', 'calendar', 'era_tables.cpp');
fs.writeFileSync(cpp, L.join('\n') + '\n');
console.log('写出:', cpp, fs.statSync(cpp).size, '字节', rows.length, '条');

// ---- 测试夹具：明清民国年号锚点（人工核对过的硬事实） ----
const T = [];
const tw = (s) => T.push(s);
tw('// 本文件由 tools/gen_calendar/emit_era.js 自动生成，请勿手工编辑。');
tw('// 年号锚点：明清与民国的年号元年，均取自《中国历史纪年表》通行口径，');
tw('// 与 fixtures/era_intervals.json 独立——用于卡住年号表，防止回归。');
tw('');
tw('#pragma once');
tw('');
tw('namespace genealogy::calendar::fixtures {');
tw('');
tw('struct EraAnchor {');
tw('  const char* dynasty;');
tw('  const char* reign;');
tw('  int first_year;   // 该年号元年 = 此农历年');
tw('  int last_year;    // 该年号末年');
tw('};');
tw('');
tw('inline constexpr EraAnchor kEraAnchors[] = {');
const anchors = [
  ['明', '洪武', 1368, 1398], ['明', '建文', 1399, 1402], ['明', '永乐', 1403, 1424],
  ['明', '洪熙', 1425, 1425], ['明', '宣德', 1426, 1435], ['明', '正统', 1436, 1449],
  ['明', '景泰', 1450, 1457], ['明', '天顺', 1457, 1464], ['明', '成化', 1465, 1487],
  ['明', '弘治', 1488, 1505], ['明', '正德', 1506, 1521], ['明', '嘉靖', 1522, 1566],
  ['明', '隆庆', 1567, 1572], ['明', '万历', 1573, 1620], ['明', '泰昌', 1620, 1620],
  ['明', '天启', 1621, 1627], ['明', '崇祯', 1628, 1644],
  ['清', '顺治', 1644, 1661], ['清', '康熙', 1662, 1722], ['清', '雍正', 1723, 1735],
  ['清', '乾隆', 1736, 1795], ['清', '嘉庆', 1796, 1820], ['清', '道光', 1821, 1850],
  ['清', '咸丰', 1851, 1861], ['清', '同治', 1862, 1874], ['清', '光绪', 1875, 1908],
  ['清', '宣统', 1909, 1911], ['中华民国', '民国', 1912, 1949],
];
for (const [d, r, a, b] of anchors) tw(`  {${q(d)}, ${q(r)}, ${a}, ${b}},`);
tw('};');
tw(`inline constexpr int kEraAnchorCount = ${anchors.length};`);
tw('');
tw('}  // namespace genealogy::calendar::fixtures');

const hpp = path.join(__dirname, '..', '..', 'core', 'tests', 'era_fixtures.hpp');
fs.writeFileSync(hpp, T.join('\n') + '\n');
console.log('写出:', hpp, fs.statSync(hpp).size, '字节', anchors.length, '锚点');
