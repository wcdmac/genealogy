#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>

namespace genealogy {

enum class Calendar { Gregorian, Lunar };

enum class DatePrecision { Day, Month, Year, Decade, Fuzzy };

// 年号纪年（如"同治三年"）。
struct Era {
  std::string name;  // 年号
  int year = 0;      // 年号内年份
};

// 多历法复合日期值对象（设计文档 4.3）：
// - 原文优先：lunar / era 为谱面原文，gregorian 为换算结果，二者分离存储；
// - 精度语义：precision 只标最小可信粒度，校验引擎据此放宽矛盾检测；
// - 具体的公历↔农历互转属历法引擎（calendar::to_chinese / to_gregorian，
//   M1.4，覆盖 1583-3000）；本类只做照录、校验、年干支与年号识别换算。
class GenealogyDate {
 public:
  Calendar calendar = Calendar::Gregorian;
  std::optional<std::string> gregorian;  // ISO：yyyy | yyyy-mm | yyyy-mm-dd
  std::optional<std::string> lunar;      // 谱面原文，如"同治三年四月初四"
  std::optional<std::string> ganzhi;     // 干支原文，如"甲子年"
  std::optional<Era> era;                // 年号纪年
  DatePrecision precision = DatePrecision::Day;
  std::optional<std::string> qualifier;  // 约 / 以前 / 以后 / 冬至 …

  // 工厂：ISO 日期。按输入粒度推断 precision，并推算年干支。
  static Result<GenealogyDate> from_iso(std::string_view iso);

  // 工厂：老谱原文照录（不猜测换算；calendar=lunar，precision=fuzzy）。
  static Result<GenealogyDate> from_raw(std::string_view text);

  nlohmann::json to_json() const;
  static Result<GenealogyDate> from_json(const nlohmann::json& j);

  // 派生：从 gregorian 字段解析公历年份（失败返回 nullopt）。
  std::optional<int> gregorian_year() const;

  // 派生：由公历年份推算年干支（以公元 4 年为甲子）。
  std::optional<std::string> year_ganzhi() const;

  // 派生：年号纪年 → （农历）年份，如 同治三年 → 1864。
  // 年号不可解（不认识 / 重名且冲突 / 年份越界）时返回 nullopt。
  std::optional<int> era_year() const;

  // 派生：可用于排序与检索的年份——优先公历年份，其次由年号纪年推出。
  // 注意：由年号推出的年份是换算结果，与谱面原文可能差一个岁首，慎作精确比对。
  std::optional<int> resolved_year() const;

  // 展示串：公历 + 原文 + 年号 + 干支，缺省项自动省略。
  std::string display() const;

  bool is_empty() const noexcept;

  // 精确比较键：用于排序（无公历信息时按 nullopt 处理，调用方需自判）。
  std::optional<std::string> sort_key() const;
};

}  // namespace genealogy
