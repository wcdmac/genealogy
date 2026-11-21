#include <genealogy/calendar/chinese_calendar.hpp>

#include <genealogy/calendar/calendar_tables.hpp>

namespace genealogy::calendar {

namespace {

// Howard Hinnant 的民用历换算。注意：C/C++ 整型除法向零截断，
// 负年份必须依赖该语义，不可用 floor 语义替代（否则 1970 年前整体差一天）。
int days_from_civil_impl(int y, unsigned m, unsigned d) noexcept {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);          // [0, 399]
  const unsigned doy =
      (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;                 // [0, 365]
  const unsigned doe =
      yoe * 365 + yoe / 4 - yoe / 100 + doy;                          // [0, 146096]
  return era * 146097 + static_cast<int>(doe) - 719468;
}

void civil_from_days_impl(int z, int* y, unsigned* m, unsigned* d) noexcept {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);        // [0, 146096]
  const unsigned yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;           // [0, 399]
  const int yy = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);        // [0, 365]
  const unsigned mp = (5 * doy + 2) / 153;                             // [0, 11]
  const unsigned dd = doy - (153 * mp + 2) / 5 + 1;                    // [1, 31]
  const unsigned mm = mp < 10 ? mp + 3 : mp - 9;                       // [1, 12]
  *y = yy + (mm <= 2);
  *m = mm;
  *d = dd;
}

constexpr char kGan[] = "甲乙丙丁戊己庚辛壬癸";   // UTF-8，每字 3 字节
constexpr char kZhi[] = "子丑寅卯辰巳午未申酉戌亥";
constexpr char kZodiac[][4] = {"鼠", "牛", "虎", "兔", "龙", "蛇",
                               "马", "羊", "猴", "鸡", "狗", "猪"};

// 天干/地支各取一字（UTF-8 3 字节）
std::string gan(int i) { return std::string(kGan + i * 3, 3); }
std::string zhi(int i) { return std::string(kZhi + i * 3, 3); }

int mod60(long long v) noexcept {
  const long long r = v % 60;
  return static_cast<int>(r < 0 ? r + 60 : r);
}

// ---- 儒略历史实口径（M9.2）--------------------------------------------
// 1582-10-15（格里历改革首日）之前的民用日期按儒略历读写，之后按格里历；
// 1582-10-05~14 在改革中被删去，不是合法民用日期。
constexpr int kReformYear = 1582, kReformMonth = 10, kReformFirstDay = 15;

// 改革首日的物理日序（最后一个儒略历标签日的次日）
const int kReformDayno =
    days_from_civil_impl(kReformYear, kReformMonth, kReformFirstDay);

// 该民用日期是否在改革首日之前（按儒略历解读）
bool before_reform(int y, int m, int d) noexcept {
  if (y != kReformYear) return y < kReformYear;
  if (m != kReformMonth) return m < kReformMonth;
  return d < kReformFirstDay;
}

// 改革中被删去的 10 天（该「日期」不存在）
bool in_reform_gap(int y, int m, int d) noexcept {
  return y == kReformYear && m == kReformMonth && d >= 5 && d <= 14;
}

// 儒略历落后格里历的天数（时代值）
int julian_offset(int y) noexcept {
  return y <= kReformYear ? y / 100 - y / 400 - 2 : 0;
}

// 儒略 y-01-01 的物理日序 = proleptic-Gregorian(y,1,1) + 上一年的时代值。
// 世纪闰年（1400/1500：儒略有 2-29 而格里历没有）的落后要到 3-01 才 +1，
// 故 1 月锚点取 y-1 的时代值；doy 是「从 1 月 1 日起的物理日数」，
// 锚定后逐日计数不再涉及 offset。
int julian_anchor(int y) noexcept {
  return days_from_civil_impl(y, 1, 1) + julian_offset(y - 1);
}

// 儒略历闰年：能被 4 整除即闰（世纪年也闰——1400/1500 都是 366 天）
bool julian_leap(int y) noexcept { return y % 4 == 0; }

int days_from_julian(int y, int m, int d) noexcept {
  static constexpr int kDim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int doy = d;
  for (int i = 1; i < m; ++i) {
    doy += kDim[i - 1] + (i == 2 && julian_leap(y) ? 1 : 0);
  }
  return julian_anchor(y) + doy - 1;
}

// 物理日序 → 儒略历日期。只对改革前的物理日调用（z ≤ 1582-10-04），
// 儒略年可能延伸到格里次年年初，故候选年查 proleptic 年及其前一年。
void julian_from_days(int z, int* y, int* m, int* d) noexcept {
  static constexpr int kDim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int yp = 0;
  unsigned mp = 0, dp = 0;
  civil_from_days_impl(z, &yp, &mp, &dp);
  const int yy = z >= julian_anchor(yp) ? yp : yp - 1;
  int jdoy = z - julian_anchor(yy) + 1;
  const bool leap = julian_leap(yy);
  int mo = 1;
  while (mo <= 12) {
    const int dim = kDim[mo - 1] + (mo == 2 && leap ? 1 : 0);
    if (jdoy <= dim) break;
    jdoy -= dim;
    ++mo;
  }
  *y = yy;
  *m = mo;
  *d = jdoy;
}

// 民用日期（儒略历史实口径）→ 物理日序
int dayno_of_civil(int y, int m, int d) noexcept {
  return before_reform(y, m, d) ? days_from_julian(y, m, d)
                                : days_from_civil_impl(y, static_cast<unsigned>(m),
                                                       static_cast<unsigned>(d));
}
int mod10(long long v) noexcept {
  const long long r = v % 10;
  return static_cast<int>(r < 0 ? r + 10 : r);
}
int mod12(long long v) noexcept {
  const long long r = v % 12;
  return static_cast<int>(r < 0 ? r + 12 : r);
}

const LunarYearRecord* record_of(int index_year) noexcept {
  if (!supports(index_year)) return nullptr;
  return &lunar_years()[index_year - first_year()];
}

// 时间序第 k 个月对应的月号（闰月记负）
int month_number_at(const LunarYearRecord& rec, int k) noexcept {
  int idx = 0;
  for (int n = 1; n <= 12; ++n) {
    if (idx == k) return n;
    ++idx;
    if (rec.leap_after == n) {
      if (idx == k) return -n;
      ++idx;
    }
  }
  return 0;
}

int month_length(const LunarYearRecord& rec, int k) noexcept {
  return (rec.len_mask >> k) & 1u ? 30 : 29;
}

}  // namespace

int days_from_civil(int y, int m, int d) noexcept {
  return days_from_civil_impl(y, static_cast<unsigned>(m),
                              static_cast<unsigned>(d));
}

void civil_from_days(int z, int* y, int* m, int* d) noexcept {
  unsigned mm = 0, dd = 0;
  civil_from_days_impl(z, y, &mm, &dd);
  *m = static_cast<int>(mm);
  *d = static_cast<int>(dd);
}

int first_supported_year() noexcept { return calendar::first_year(); }
int last_supported_year() noexcept { return calendar::last_year(); }

bool supports(int gregorian_year) noexcept {
  return gregorian_year >= first_year() && gregorian_year <= last_year();
}

// ---------------------------------------------------------------- 互转

// 民用日期合法性（第五轮 高-3）：供 to_chinese/day_ganzhi/solar_term_on 共用。
// 算术归一化语义不该泄漏为公开 API——{2000,2,30}、{1500,20,1} 都不能
// 悄悄进 dayno_of_civil：后者按 kDim[month-1] 取值，month>12 会越界读。
bool valid_civil_date(const GregorianDate& date) noexcept {
  if (date.month < 1 || date.month > 12 || date.day < 1) return false;
  static constexpr int kDim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = before_reform(date.year, date.month, date.day)
                        ? julian_leap(date.year)
                        : (date.year % 4 == 0 && date.year % 100 != 0) ||
                              date.year % 400 == 0;
  const int dim = kDim[date.month - 1] + (date.month == 2 && leap ? 1 : 0);
  if (date.day > dim) return false;
  return !in_reform_gap(date.year, date.month, date.day);
}

Result<ChineseDate> to_chinese(const GregorianDate& date) {
  // 严格校验民用日期（M2 + 第五轮 高-3）：三段错误分别给出可操作提示
  if (date.month < 1 || date.month > 12 || date.day < 1) {
    return Error{ErrorCode::InvalidArgument,
                 "月日超出民用历范围: " + std::to_string(date.year) + "-" +
                     std::to_string(date.month) + "-" +
                     std::to_string(date.day)};
  }
  static constexpr int kDimDetail[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leapDetail = before_reform(date.year, date.month, date.day)
                              ? julian_leap(date.year)
                              : (date.year % 4 == 0 && date.year % 100 != 0) ||
                                    date.year % 400 == 0;
  const int dim = kDimDetail[date.month - 1] +
                  (date.month == 2 && leapDetail ? 1 : 0);
  if (date.day > dim) {
    return Error{ErrorCode::InvalidArgument,
                 "该月只有 " + std::to_string(dim) + " 天: " +
                     std::to_string(date.year) + "-" +
                     std::to_string(date.month) + "-" +
                     std::to_string(date.day)};
  }
  if (in_reform_gap(date.year, date.month, date.day)) {
    return Error{ErrorCode::InvalidArgument,
                 "1582-10-05 至 10-14 在格里历改革中被删去，不是合法民用日期"};
  }
  const int z = dayno_of_civil(date.year, date.month, date.day);
  int g = date.year;
  const LunarYearRecord* rec = record_of(g);
  if (rec == nullptr) {
    return Error{ErrorCode::CalendarOutOfRange,
                 "历法数据不覆盖该年份: " + std::to_string(g)};
  }
  if (z < rec->first_day) {   // 岁首之前属于上一农历年
    --g;
    rec = record_of(g);
    if (rec == nullptr) {
      return Error{ErrorCode::CalendarOutOfRange,
                   "历法数据不覆盖该日期（岁首前一日属上一年）: " +
                       std::to_string(date.year)};
    }
  }
  ChineseDate out;
  out.year = g;
  int start = rec->first_day;
  for (int k = 0; k < rec->month_count; ++k) {
    const int len = month_length(*rec, k);
    if (z < start + len) {
      out.month = month_number_at(*rec, k);
      out.day = z - start + 1;
      return out;
    }
    start += len;
  }
  return Error{ErrorCode::Internal, "月序遍历越界（数据表损坏？）"};
}

Result<GregorianDate> to_gregorian(const ChineseDate& date) {
  const LunarYearRecord* rec = record_of(date.year);
  if (rec == nullptr) {
    return Error{ErrorCode::CalendarOutOfRange,
                 "历法数据不覆盖该农历年: " + std::to_string(date.year)};
  }
  int start = rec->first_day;
  for (int k = 0; k < rec->month_count; ++k) {
    const int len = month_length(*rec, k);
    if (month_number_at(*rec, k) == date.month) {
      if (date.day < 1 || date.day > len) {
        return Error{ErrorCode::InvalidArgument,
                     "该农历月只有 " + std::to_string(len) + " 天，给定 " +
                         std::to_string(date.day)};
      }
      const int z = start + date.day - 1;
      GregorianDate out;
      // 改革首日之前的物理日按儒略历标签输出
      if (z < kReformDayno) {
        julian_from_days(z, &out.year, &out.month, &out.day);
      } else {
        civil_from_days(z, &out.year, &out.month, &out.day);
      }
      return out;
    }
    start += len;
  }
  return Error{ErrorCode::NotFound,
               "该农历年无此月: " + std::to_string(date.month)};
}

// ---------------------------------------------------------------- 名称

std::string month_name(int month) {
  const int n = month < 0 ? -month : month;
  static const char* const kNames[] = {"",    "正月", "二月", "三月", "四月",
                                       "五月", "六月", "七月", "八月", "九月",
                                       "十月", "冬月", "腊月"};
  if (n < 1 || n > 12) return {};
  const std::string base = kNames[n];
  return month < 0 ? "闰" + base : base;
}

std::string day_name(int day) {
  static const char* const kNames[] = {
      "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八",
      "初九", "初十", "十一", "十二", "十三", "十四", "十五", "十六",
      "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三", "廿四",
      "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};
  if (day < 1 || day > 30) return {};
  return kNames[day - 1];
}

std::string ganzhi_name(int index) {
  const int i = mod60(index);
  return gan(i % 10) + zhi(i % 12);
}

// ---------------------------------------------------------------- 干支 / 生肖

std::string year_ganzhi(int lunar_year) {
  // 公元 4 年为甲子，故 序号 = (年 - 4) mod 60
  return ganzhi_name(mod60(static_cast<long long>(lunar_year) - 4));
}

std::string month_ganzhi(int lunar_year, int month) {
  // 五虎遁：正月建寅（地支 2），天干由年干推
  const int year_stem = mod60(static_cast<long long>(lunar_year) - 4) % 10;
  const int n = month < 0 ? -month : month;
  const int stem = mod10(2LL * year_stem + 2 + (n - 1));
  const int branch = mod12(2LL + (n - 1));   // 正月 = 寅
  return gan(stem) + zhi(branch);
}

std::string day_ganzhi(const GregorianDate& date) {
  // 第五轮 高-3：公开 API 必须自己校验，否则 {1500,20,1} 会让
  // days_from_julian 越界读 kDim[12..]。非法日期返回空串（该 API 无 Result 形态）。
  if (!valid_civil_date(date)) return {};
  // 儒略日 JDN = 日序号 + 2440588；日干支 = (JDN + 49) mod 60
  // 输入按儒略历史实口径解读（改革前儒略历、后格里历）
  const int z = dayno_of_civil(date.year, date.month, date.day);
  return ganzhi_name(mod60(static_cast<long long>(z) + 2440588 + 49));
}

std::string zodiac(int lunar_year) {
  const int branch = mod60(static_cast<long long>(lunar_year) - 4) % 12;
  return kZodiac[branch];
}

// ---------------------------------------------------------------- 节气

Result<SolarTermMoment> solar_term(int year, int index) {
  if (index < 0 || index >= 24) {
    return Error{ErrorCode::InvalidArgument, "节气索引须在 0..23"};
  }
  if (year < term_first_year() || year > last_year()) {
    return Error{ErrorCode::CalendarOutOfRange,
                 "节气数据不覆盖该年份（自 " +
                     std::to_string(term_first_year()) + " 年起提供）: " +
                     std::to_string(year)};
  }
  const std::size_t off =
      static_cast<std::size_t>(year - term_first_year()) * 24 + index;
  SolarTermMoment out;
  out.index = index;
  out.name = term_names()[index];
  const int z = days_from_civil(year, 1, 1) + term_day_of_year()[off] - 1;
  civil_from_days(z, &out.date.year, &out.date.month, &out.date.day);
  out.hour = term_minute_of_day()[off] / 60;
  out.minute = term_minute_of_day()[off] % 60;
  return out;
}

std::optional<int> solar_term_on(const GregorianDate& date) {
  // 第五轮 高-3：同上，非法民用日期直接 nullopt，杜绝越界读
  if (!valid_civil_date(date)) return std::nullopt;
  const int z = dayno_of_civil(date.year, date.month, date.day);
  // 本年与上年都查一遍：年初的节气可能归属上一年份的节气表
  for (int y = date.year - 1; y <= date.year; ++y) {
    if (y < term_first_year() || y > last_year()) continue;
    const std::size_t base = static_cast<std::size_t>(y - term_first_year()) * 24;
    for (int i = 0; i < 24; ++i) {
      const int tz = days_from_civil(y, 1, 1) + term_day_of_year()[base + i] - 1;
      if (tz == z) return i;
    }
  }
  return std::nullopt;
}

}  // namespace genealogy::calendar
