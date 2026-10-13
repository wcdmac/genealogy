#include <genealogy/calendar/era.hpp>

#include <genealogy/calendar/era_tables.hpp>

namespace genealogy::calendar {

namespace {

EraInfo to_info(const EraRecord& r) {
  EraInfo out;
  out.dynasty = r.dynasty;
  out.reign = r.reign;
  out.reign_traditional = r.reign_traditional;
  out.note = r.note;
  out.start_year = r.start_year;
  out.end_year = r.end_year;
  out.mainline = r.mainline;
  return out;
}

}  // namespace

std::optional<EraInfo> era_of(int year) {
  // 主线可能在同一年有多条（改元之年重叠），取起始最晚的那条更符合直觉：
  // 1620 年既是万历四十八年，也是泰昌元年，通常说泰昌。
  std::optional<EraInfo> best;
  for (std::size_t i = 0; i < era_record_count(); ++i) {
    const EraRecord& r = era_records()[i];
    if (!r.mainline) continue;
    if (year < r.start_year || year > r.end_year) continue;
    if (!best || r.start_year > best->start_year) best = to_info(r);
  }
  return best;
}

std::vector<EraInfo> eras_of(int year) {
  std::vector<EraInfo> out;
  for (std::size_t i = 0; i < era_record_count(); ++i) {
    const EraRecord& r = era_records()[i];
    if (year >= r.start_year && year <= r.end_year) out.push_back(to_info(r));
  }
  return out;
}

std::vector<EraInfo> find_era(const std::string& reign) {
  std::vector<EraInfo> out;
  for (std::size_t i = 0; i < era_record_count(); ++i) {
    const EraRecord& r = era_records()[i];
    if (reign == r.reign || reign == r.reign_traditional) out.push_back(to_info(r));
  }
  return out;
}

Result<int> era_to_year(const std::string& reign, int year_in_era) {
  return era_to_year(std::string(), reign, year_in_era);
}

Result<int> era_to_year(const std::string& dynasty, const std::string& reign,
                        int year_in_era) {
  if (year_in_era < 1) {
    return Error{ErrorCode::InvalidArgument, "年号年份须 ≥ 1，给定 " +
                                                 std::to_string(year_in_era)};
  }
  auto candidates = find_era(reign);
  if (!dynasty.empty()) {
    std::vector<EraInfo> filtered;
    for (auto& c : candidates) {
      if (c.dynasty == dynasty) filtered.push_back(c);
    }
    candidates = std::move(filtered);
  }
  if (candidates.empty()) {
    return Error{ErrorCode::NotFound, "无此年号: " + reign};
  }
  std::vector<const EraInfo*> hits;
  for (const auto& c : candidates) {
    const int y = c.year_of(year_in_era);
    if (y >= c.start_year && y <= c.end_year) hits.push_back(&c);
  }
  if (hits.empty()) {
    // 名对了但年份超出该年号的实际长度
    std::string msg = "年号「" + reign + "」没有第 " +
                      std::to_string(year_in_era) + " 年；其区间：";
    for (const auto& c : candidates) {
      msg += c.dynasty + c.reign + "（" + std::to_string(c.start_year) + "-" +
             std::to_string(c.end_year) + "，共 " +
             std::to_string(c.end_year - c.start_year + 1) + " 年） ";
    }
    return Error{ErrorCode::InvalidArgument, msg};
  }
  if (hits.size() > 1) {
    std::string msg = "年号「" + reign + "」重名，第 " +
                      std::to_string(year_in_era) + " 年有多个候选：";
    for (const auto* c : hits) {
      msg += c->dynasty + c->reign;
      if (!c->note.empty()) msg += "(" + c->note + ")";
      msg += "=" + std::to_string(c->year_of(year_in_era)) + " ";
    }
    return Error{ErrorCode::Conflict, msg};
  }
  return hits.front()->year_of(year_in_era);
}

std::string chinese_number(int n) {
  static const char* const kDigits[] = {"",    "一", "二", "三", "四", "五",
                                        "六", "七", "八", "九"};
  if (n <= 0) return {};
  if (n < 10) return kDigits[n];
  if (n == 10) return "十";
  if (n < 20) return "十" + std::string(kDigits[n % 10]);
  const int tens = n / 10;
  const int ones = n % 10;
  return std::string(kDigits[tens]) + "十" + (ones ? kDigits[ones] : "");
}

std::string era_display(const EraInfo& era, int year_in_era) {
  return era.reign + chinese_number(year_in_era) + "年";
}

}  // namespace genealogy::calendar
