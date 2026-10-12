#pragma once

#include <string>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 称谓计算结果（设计文档 4.5 输出结构）。
struct Appellation {
  std::string standard;    // 标准称谓（父亲 / 伯父 / 堂兄）
  std::string colloquial;  // 口语称谓（爸爸 / 大伯 / 堂哥）
  std::string formal;      // 书面称谓（父 / 从父兄，谱式）
  std::string basis;       // 规则依据
  bool from_fallback = false;  // 规则表未收录，为组合式描述
};

struct AppellationQuery {
  std::string ego;
  std::string alter;
};

// 计算 ego 对 alter 的称谓（设计文档 5.1.2）。
Result<Appellation> compute_appellation(const PersonGraph& g,
                                        const AppellationQuery& q);

}  // namespace genealogy::lineage
