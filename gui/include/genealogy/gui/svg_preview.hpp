#pragma once

// 自家 SVG 子集预览：把 core 生成的 SVG 直接画到 ImGui DrawList。
//
// 为什么不需要光栅化依赖：SVG 由本项目自己生成，标签只有 rect/line/circle/
// path/text，样式来自 <style> 里的简易 class 规则。解析这个子集只需字符串
// 扫描；绘制用 ImGui 已有的 ImDrawList（中线、矩形、圆、文字都是原生能力）。
// 预览与导出消费同一个 SVG 字符串，像素级一致，且零新增依赖。

#include <genealogy/error.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace genealogy::gui {

struct SvgStyle {
  std::string fill;
  std::string stroke;
  float stroke_width = 1.0F;
  float font_size = 15.0F;
};

struct SvgNode {
  enum class Kind { Rect, Line, Circle, Path, Text };

  Kind kind = Kind::Rect;
  std::string cls;
  // rect
  float x = 0, y = 0, w = 0, h = 0, rx = 0;
  // line
  float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  // circle
  float cx = 0, cy = 0, r = 0;
  // text
  std::string text;
  // path
  std::string path_d;

  // 内联属性（空表示未指定，回落到 class/全局样式）
  std::string fill, stroke;
  bool has_fill = false, has_stroke = false;
  float stroke_width = 0;
  float font_size = 0;
};

struct SvgScene {
  float width = 720;
  float height = 1040;
  // class 名 → 样式；"" 为全局 text 规则
  std::map<std::string, SvgStyle> classes;
  std::vector<SvgNode> nodes;
};

// 解析本项目生成的 SVG 子集。未知标签/属性安全忽略。
Result<SvgScene> parse_own_svg(std::string_view svg);

// 统计用：节点数、class 数（无头测试断言）
std::size_t svg_node_count(const SvgScene& scene);
std::size_t svg_class_count(const SvgScene& scene);

}  // namespace genealogy::gui
