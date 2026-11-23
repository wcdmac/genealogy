#pragma once

// 古籍版式要素渲染：版心/鱼尾/象鼻/栏线/书耳/天头地脚/界行全要素。
// 图形为自行设计的 SVG 几何（公版古籍形制），不描摹任何软件素材。
// 参数来自 shared/layout/*.json（构建时嵌入）。

#include <optional>
#include <string>

#include <genealogy/error.hpp>
#include <genealogy/io/layout_geometry.hpp>
#include <genealogy/io/markup.hpp>

namespace genealogy::io {

struct LayoutOptions {
  std::string preset = "guji_classic";  // guji_classic | plain_modern
  std::string book_title;
  std::string volume_title;
  int page_number = 1;
  int total_pages = 1;
  std::vector<MarkupNode> content;      // 标记解析结果（本页）
  // 可选：覆盖预设字号度量（如 CLI --body-size 微调，无需改 JSON 重建）
  std::optional<LayoutGeometry> geometry_override;
};

// 生成带完整古籍版式要素的单页 SVG
Result<std::string> render_layout_page(const LayoutOptions& opt);

// 按 id 取预设 JSON；未知 id 返回错误
Result<std::string> layout_preset_json(const std::string& id);

// 按预设取字体度量几何——分页器与渲染器必须共用它，保证字号改动后
// 分页点与渲染坐标同步联动。
Result<LayoutGeometry> geometry_for_preset(const std::string& id);

}  // namespace genealogy::io
