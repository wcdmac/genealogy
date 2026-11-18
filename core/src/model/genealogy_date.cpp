#include <genealogy/model/genealogy_date.hpp>

#include <array>
#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/calendar/era.hpp>
#include <genealogy/calendar/era_tables.hpp>

namespace genealogy {

NLOHMANN_JSON_SERIALIZE_ENUM(Calendar, {
                                           {Calendar::Gregorian, "gregorian"},
                                           {Calendar::Lunar, "lunar"},
                                       })

NLOHMANN_JSON_SERIALIZE_ENUM(DatePrecision, {
                                                 {DatePrecision::Day, "day"},
                                                 {DatePrecision::Month, "month"},
                                                 {DatePrecision::Year, "year"},
                                                 {DatePrecision::Decade, "decade"},
                                                 {DatePrecision::Fuzzy, "fuzzy"},
                                             })

namespace {

constexpr std::array<std::string_view, 10> kStems = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
constexpr std::array<std::string_view, 12> kBranches = {
    "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};

std::string trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(first, last - first + 1));
}

std::optional<int> to_int(std::string_view s) {
  int value = 0;
  const auto [ptr, ec] =
      std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc() || ptr != s.data() + s.size()) return std::nullopt;
  return value;
}

// 中文数字 → 阿拉伯数字，支持 一..九十九（元年…六十一年）。
// 只认「十/十一/二十/二十一」这类常规写法，异体（廿、卅）不处理。
std::optional<int> parse_chinese_number(std::string_view s) {
  static constexpr std::string_view kDigits[] = {"零", "一", "二", "三", "四",
                                                 "五", "六", "七", "八", "九"};
  if (s.empty()) return std::nullopt;
  if (s == "元") return 1;   // 元年
  if (s == "十") return 10;
  // UTF-8 下必须用字符串字面量查找——'十' 是多字节字符字面量，窄化成单字节后查不到
  const auto ten = s.find("十");
  if (ten == std::string_view::npos) {
    for (int i = 1; i <= 9; ++i) {
      if (s == kDigits[i]) return i;
    }
    return std::nullopt;
  }
  int tens = 1;
  if (ten != 0) {
    const std::string_view head = s.substr(0, ten);
    bool found = false;
    for (int i = 1; i <= 9; ++i) {
      if (head == kDigits[i]) {
        tens = i;
        found = true;
        break;
      }
    }
    if (!found) return std::nullopt;
  }
  const std::string_view tail = s.substr(ten + 3);   // 「十」占 3 字节
  if (tail.empty()) return tens * 10;
  for (int i = 1; i <= 9; ++i) {
    if (tail == kDigits[i]) return tens * 10 + i;
  }
  return std::nullopt;
}

// 从老谱原文开头识别「年号 + 中文数字 + 年」，如「同治三年四月初四」。
// 年号名以年号表为准（简繁皆可），命中则填 era，其余原文照录不动。
std::optional<Era> parse_era_prefix(const std::string& text) {
  std::optional<Era> best;
  std::size_t best_len = 0;
  for (std::size_t i = 0; i < calendar::era_record_count(); ++i) {
    const auto& rec = calendar::era_records()[i];
    for (const char* name : {rec.reign, rec.reign_traditional}) {
      const std::string_view n(name);
      if (n.empty() || !text.starts_with(n)) continue;
      const std::size_t rest = n.size();
      if (rest + 3 > text.size()) continue;                  // 至少要有「X年」
      if (text.compare(rest, 3, "年") == 0) continue;        // 光有年号没有年份
      const auto year_end = text.find("年", rest);
      if (year_end == std::string::npos) continue;
      const auto num = parse_chinese_number(
          std::string_view(text).substr(rest, year_end - rest));
      if (!num) continue;
      if (n.size() > best_len) {
        best_len = n.size();
        // 归一到简体年号（谱面可能写繁体「萬曆」，内部统一存「万历」）
        best = Era{std::string(rec.reign), *num};
      }
    }
  }
  return best;
}

bool valid_ymd(int year, int month, int day) {
  if (year < 1 || year > 9999) return false;
  if (month < 1 || month > 12) return false;
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  int max_day = kDays[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
    max_day = 29;
  }
  return day >= 1 && day <= max_day;
}

}  // namespace

Result<GenealogyDate> GenealogyDate::from_iso(std::string_view iso) {
  const std::string s = trim(iso);
  GenealogyDate d;
  d.calendar = Calendar::Gregorian;

  if (s.size() == 4) {
    const auto y = to_int(s);
    if (!y || !valid_ymd(*y, 1, 1)) {
      return invalid_argument("ISO 年份非法: " + s);
    }
    d.gregorian = s;
    d.precision = DatePrecision::Year;
  } else if (s.size() == 7 && s[4] == '-') {
    const auto y = to_int(s.substr(0, 4));
    const auto m = to_int(s.substr(5, 2));
    if (!y || !m || !valid_ymd(*y, *m, 1)) {
      return invalid_argument("ISO 年月非法: " + s);
    }
    d.gregorian = s;
    d.precision = DatePrecision::Month;
  } else if (s.size() == 10 && s[4] == '-' && s[7] == '-') {
    const auto y = to_int(s.substr(0, 4));
    const auto m = to_int(s.substr(5, 2));
    const auto day = to_int(s.substr(8, 2));
    if (!y || !m || !day || !valid_ymd(*y, *m, *day)) {
      return invalid_argument("ISO 日期非法: " + s);
    }
    d.gregorian = s;
    d.precision = DatePrecision::Day;
  } else {
    return invalid_argument("ISO 日期应为 yyyy / yyyy-mm / yyyy-mm-dd: " + s);
  }

  d.ganzhi = d.year_ganzhi();
  return d;
}

Result<GenealogyDate> GenealogyDate::from_raw(std::string_view text) {
  const std::string s = trim(text);
  if (s.empty()) return invalid_argument("原文日期不能为空");
  GenealogyDate d;
  d.calendar = Calendar::Lunar;
  d.lunar = s;
  d.precision = DatePrecision::Fuzzy;
  // 谱面常以年号起头（「同治三年四月初四」），能认出就把年号单列出来，
  // 便于后续按年号排序与换算；认不出就原样照录，绝不猜测。
  if (const auto era = parse_era_prefix(s)) d.era = era;
  return d;
}

nlohmann::json GenealogyDate::to_json() const {
  nlohmann::ordered_json j;
  j["calendar"] = calendar;
  if (gregorian) j["gregorian"] = *gregorian;
  if (lunar) j["lunar"] = *lunar;
  if (ganzhi) j["ganzhi"] = *ganzhi;
  if (era) {
    j["era"] = nlohmann::ordered_json{{"name", era->name}, {"year", era->year}};
  }
  j["precision"] = precision;
  if (qualifier) j["qualifier"] = *qualifier;
  return j;
}

Result<GenealogyDate> GenealogyDate::from_json(const nlohmann::json& j) {
  if (!j.is_object()) return invalid_argument("date json 必须是对象");
  GenealogyDate d;
  try {
    if (const auto it = j.find("calendar"); it != j.end() && !it->is_null()) {
      const auto s = it->get<std::string>();
      if (s == "gregorian") {
        d.calendar = Calendar::Gregorian;
      } else if (s == "lunar") {
        d.calendar = Calendar::Lunar;
      } else {
        return invalid_argument("未知 calendar: " + s);
      }
    }
    if (const auto it = j.find("gregorian"); it != j.end() && !it->is_null()) {
      // 复用 from_iso 的格式与范围校验（2026-11 静态审查 M1）——此前
      // 原样落库，"9999-99-99" 会破坏排序键与干支推算
      const std::string g = it->get<std::string>();
      if (!g.empty()) {
        auto chk = from_iso(g);
        if (!chk) return chk.error();
      }
      d.gregorian = g;
    }
    if (const auto it = j.find("lunar"); it != j.end() && !it->is_null()) {
      // lunar 是谱面原文（from_raw 口径，自由文本），不做过强校验
      d.lunar = it->get<std::string>();
    }
    // ganzhi 不收客户端值：ganzhi 由公历日期推导（下方），信任客户端
    // 回传的干支会污染纪日一致性
    if (const auto it = j.find("era"); it != j.end() && !it->is_null()) {
      Era era;
      era.name = it->at("name").get<std::string>();
      era.year = it->at("year").get<int>();
      // 年号合法性：名称须在年号表中存在、年份在现实域（1..300——
      // 最长在位超过 60 年的只有康熙/乾隆，300 已是 5 倍余量）
      const auto found = calendar::find_era(era.name);
      if (found.empty()) {
        return invalid_argument("未知年号: " + era.name);
      }
      if (era.year < 1 || era.year > 300) {
        return invalid_argument("年号年份应在 1..300: " +
                                std::to_string(era.year));
      }
      d.era = era;
    }
    if (const auto it = j.find("precision"); it != j.end() && !it->is_null()) {
      const auto s = it->get<std::string>();
      if (s == "day") {
        d.precision = DatePrecision::Day;
      } else if (s == "month") {
        d.precision = DatePrecision::Month;
      } else if (s == "year") {
        d.precision = DatePrecision::Year;
      } else if (s == "decade") {
        d.precision = DatePrecision::Decade;
      } else if (s == "fuzzy") {
        d.precision = DatePrecision::Fuzzy;
      } else {
        return invalid_argument("未知 precision: " + s);
      }
    }
    // 干支按公历重算（不信任客户端回传）
    if (d.gregorian && !d.gregorian->empty()) {
      d.ganzhi = d.year_ganzhi();
    }
    if (const auto it = j.find("qualifier"); it != j.end() && !it->is_null()) {
      d.qualifier = it->get<std::string>();
    }
  } catch (const nlohmann::json::exception& e) {
    return invalid_argument(std::string("date json 解析失败: ") + e.what());
  }
  return d;
}

std::optional<int> GenealogyDate::gregorian_year() const {
  if (!gregorian || gregorian->size() < 4) return std::nullopt;
  return to_int(std::string_view(*gregorian).substr(0, 4));
}

std::optional<int> GenealogyDate::era_year() const {
  if (!era) return std::nullopt;
  const auto y = calendar::era_to_year(era->name, era->year);
  if (!y) return std::nullopt;
  return *y;
}

std::optional<int> GenealogyDate::resolved_year() const {
  if (const auto y = gregorian_year()) return y;
  return era_year();   // 无公历信息时退回年号纪年（换算结果，非原文）
}

std::optional<std::string> GenealogyDate::year_ganzhi() const {
  const auto y = gregorian_year();
  if (!y || *y < 4) return std::nullopt;  // 公元 4 年为甲子
  const int offset = *y - 4;
  return std::string(kStems[static_cast<std::size_t>(offset % 10)]) +
         std::string(kBranches[static_cast<std::size_t>(offset % 12)]);
}

std::string GenealogyDate::display() const {
  if (is_empty()) return "不详";
  std::vector<std::string> parts;
  if (era) {
    parts.push_back(era->name + calendar::chinese_number(era->year) + "年");
  }
  if (lunar) parts.push_back(*lunar);
  if (ganzhi) parts.push_back(*ganzhi);
  if (gregorian) parts.push_back(*gregorian);
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) out += " · ";
    out += parts[i];
  }
  if (qualifier) out = *qualifier + out;
  return out;
}

bool GenealogyDate::is_empty() const noexcept {
  return !gregorian && !lunar && !era && !ganzhi;
}

std::optional<std::string> GenealogyDate::sort_key() const {
  return gregorian;  // ISO 串字典序即时间序；无公历信息时由调用方处理
}

}  // namespace genealogy
