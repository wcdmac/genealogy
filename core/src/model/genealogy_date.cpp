#include <genealogy/model/genealogy_date.hpp>

#include <array>
#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

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
      d.gregorian = it->get<std::string>();
    }
    if (const auto it = j.find("lunar"); it != j.end() && !it->is_null()) {
      d.lunar = it->get<std::string>();
    }
    if (const auto it = j.find("ganzhi"); it != j.end() && !it->is_null()) {
      d.ganzhi = it->get<std::string>();
    }
    if (const auto it = j.find("era"); it != j.end() && !it->is_null()) {
      Era era;
      era.name = it->at("name").get<std::string>();
      era.year = it->at("year").get<int>();
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
  if (era) parts.push_back(era->name + std::to_string(era->year) + "年");
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
