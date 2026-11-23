#pragma once

// P1 世系图：纯 C++ 生成 SVG（自有代码，规避 Graphviz 的 EPL 义务）。
// 支持六种体例；可读性特性（焦点/折叠/搜索/分色/环标注）由 SVG 内嵌
// 的原生 CSS/JS 实现，无第三方依赖、离线可用。
//
// 数据来自 core 的 PersonGraph（权威亲子/配偶边），不另建关系解析。

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/person.hpp>

namespace genealogy::io {

enum class ChartStyle {
  Pedigree,     // 祖先图：向上每层翻倍，可预测网格
  Descendant,   // 后代图：向下分层，兄弟共横线 + 配偶等号
  Hourglass,    // 沙漏图：中心一人，上祖先下后代
  Wufu,         // 五服图：九族纵轴 × 五等服制
  European,     // 欧式：横行分格，五世一图（右→左）
  Soviet,       // 苏式：垂珠竖线，五世一图，跨页重叠一世
};

struct ChartOptions {
  const lineage::PersonGraph* graph = nullptr;
  ChartStyle style_hint = ChartStyle::Descendant;  // 要生成的体例
  std::string focus_id;              // 中心人物（pedigree/hourglass 必填）
  int depth = 5;                     // 展开代数
  int page_size = 5;                 // 欧/苏式每图世数（默认五世一图）
  bool show_spouses = true;          // 显示配偶边
  std::string title;                 // 图标题
};

struct ChartResult {
  std::string svg;                   // 单页 SVG（含内嵌样式与脚本）
  std::size_t node_count = 0;
  std::size_t page_count = 1;        // 欧/苏式跨页数
  std::vector<std::string> warnings; // 环检测/断链提示
};

Result<ChartResult> render_chart_svg(const ChartOptions& opt);
const char* to_string(ChartStyle s);
// 从字符串解析体例名，失败返回 nullopt
std::optional<ChartStyle> chart_style_from_string(const std::string& s);

}  // namespace genealogy::io
