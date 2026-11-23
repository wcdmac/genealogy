#pragma once

// SVG 预览的 ImGui 绘制层（依赖 ImGui，与纯解析层 svg_preview.hpp 分离）。

#include <genealogy/gui/svg_preview.hpp>

#include <imgui.h>

namespace genealogy::gui {

void draw_svg_preview(const SvgScene& scene, ImDrawList* dl,
                      const ImVec2& origin, float scale);

}  // namespace genealogy::gui
