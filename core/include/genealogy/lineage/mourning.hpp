#pragma once

#include <optional>
#include <string>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 丧服等级（五服 + 袒免 + 无服；设计文档 4.4.3）。
enum class MourningGrade {
  ZhanCui,        // 斩衰
  QiCuiZhang,     // 齐衰杖期
  QiCuiBuZhang,   // 齐衰不杖期
  QiCuiWuYue,     // 齐衰五月
  QiCuiSanYue,    // 齐衰三月
  DaGong,         // 大功
  XiaoGong,       // 小功
  SiMa,           // 缌麻
  TanMian,        // 袒免
  WuFu,           // 无服
};

struct MourningDegree {
  MourningGrade grade = MourningGrade::WuFu;
  std::string garment_name;  // 斩衰 / 齐衰杖期 / ...
  std::string period;        // 三年 / 一年 / ...
  int period_months = 0;     // 27 / 13 / 9 ...
  // 命中的规则标识：规则表里以 path（F.M.B+.S 这类步骤序列）为主键，
  // 同一路径的不同条件分支共用它，故它标识「条文」而非「某一次匹配」。
  // 兜底推断（规则表未收录）时固定为 "fallback"。
  std::string rule_id;
  std::string basis;         // 依据条文
  std::string ruleset;       // ming_qing
  bool from_fallback = false;  // 规则表未收录，按世代距兜底推断
};

// 等级 → 规则表里的 garment 标识（REST 的 garment 字段、客户端本地化都用它）。
// 与 grade_from_id 互为反函数；未知值返回 "wufu" 并置 ok=false。
const char* to_string(MourningGrade grade);
std::optional<MourningGrade> mourning_grade_from_string(const std::string& s);

struct MourningQuery {
  std::string ego;
  std::string alter;
  std::string ruleset = "ming_qing";  // 默认明清定型版
};

// 可用规则表（数据外置，ADR-6）。
std::vector<std::string> available_rulesets();

// 计算 ego 为 alter 的服制（设计文档 5.1.2）。
Result<MourningDegree> compute_mourning_degree(const PersonGraph& g,
                                               const MourningQuery& q);

}  // namespace genealogy::lineage
