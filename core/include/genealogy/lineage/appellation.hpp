#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 称谓计算结果（设计文档 4.5 输出结构）。
struct Appellation {
  std::string standard;    // 标准称谓（父亲 / 伯父 / 堂兄）
  std::string colloquial;  // 口语称谓（爸爸 / 大伯 / 堂哥）
  std::string formal;      // 书面称谓（父 / 从父兄，谱式）
  // 对方是否为长辈（辈分差 < 0）。同辈时为 false——长幼另看 path 末步的
  // B+/Z+ 标记，这里只回答「尊卑」，不与「长幼」混为一谈。
  bool elder = false;
  // 关系路径（规则表字母序列，如 ["F","M","B+"]）。兜底描述也带路径，
  // 便于谱学顾问据此补录规则。
  std::vector<std::string> path;
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
