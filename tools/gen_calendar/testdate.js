// 日期序号算法（days since 1970-01-01）往返自检。
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
const leap = (y) => (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;

let bad = 0, n = 0;
for (let y = -1000; y <= 3200; y++) {
  for (let m = 1; m <= 12; m++) {
    const dim = DIM[m - 1] + (m === 2 && leap(y) ? 1 : 0);
    for (const d of [1, 15, dim]) {
      n++;
      const z = daysFromCivil(y, m, d);
      const [Y, M, D] = civilFromDays(z);
      if (Y !== y || M !== m || D !== d) {
        bad++;
        if (bad < 6) console.log('BAD', y, m, d, '->', z, '->', Y, M, D);
      }
    }
  }
}
console.log('往返样本', n, '不符', bad);
console.log('关键点 0000-02-24 ->', civilFromDays(daysFromCivil(0, 2, 24)).join('-'));
console.log('关键点 0001-01-01 ->', civilFromDays(daysFromCivil(1, 1, 1)).join('-'));
