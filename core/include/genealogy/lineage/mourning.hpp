#pragma once

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
  std::string basis;         // 依据条文
  std::string ruleset;       // ming_qing
  bool from_fallback = false;  // 规则表未收录，按世代距兜底推断
};

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
