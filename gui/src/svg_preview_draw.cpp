// 自家 SVG 子集 → ImGui DrawList（无光栅化、无新依赖）。

#include <genealogy/gui/svg_preview_draw.hpp>

#include <genealogy/util/utf8.hpp>

#include <imgui.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace genealogy::gui {

namespace {

unsigned color_of(const std::string& spec) {
  if (spec.empty() || spec == "none" || spec == "transparent") return 0;
  std::string s = spec;
  if (!s.empty() && s[0] == '#') s.erase(0, 1);
  if (s.size() == 3) {
    std::string full;
    for (char c : s) full += std::string(2, c);
    s = full;
  }
  if (s.size() < 6) return 0xFF1A1A1A;
  unsigned r = 0, g = 0, b = 0;
  try {
    r = static_cast<unsigned>(std::stoul(s.substr(0, 2), nullptr, 16));
    g = static_cast<unsigned>(std::stoul(s.substr(2, 2), nullptr, 16));
    b = static_cast<unsigned>(std::stoul(s.substr(4, 2), nullptr, 16));
  } catch (...) {
    return 0xFF1A1A1A;
  }
  return (0xFFu << 24) | (b << 16) | (g << 8) | r;  // IMGUI_COLOR ABGR
}

struct Resolved {
  std::string fill, stroke;
  float stroke_width = 1;
  float font_size = 15;
};

Resolved resolve(const SvgScene& scene, const SvgNode& n) {
  SvgStyle base;
  auto it = scene.classes.find(n.cls);
  if (it != scene.classes.end()) base = it->second;
  else {
    auto g = scene.classes.find("");
    if (g != scene.classes.end()) base = g->second;
  }
  Resolved r;
  r.fill = n.has_fill ? n.fill : base.fill;
  r.stroke = n.has_stroke ? n.stroke : base.stroke;
  r.stroke_width = n.stroke_width > 0 ? n.stroke_width : base.stroke_width;
  r.font_size = n.font_size > 0 ? n.font_size : base.font_size;
  return r;
}

// path d 只含 M/L/Q/Z（自家生成器）。Q 折线近似，供预览。
std::vector<std::pair<ImVec2, ImVec2>> path_segments(std::string_view d,
                                                      float sx, float sy,
                                                      float scale, ImVec2 org) {
  std::vector<std::pair<ImVec2, ImVec2>> segs;
  std::size_t i = 0;
  char cmd = 0;
  ImVec2 cur{0, 0}, start{0, 0};
  auto number = [&]() -> float {
    while (i < d.size() && (d[i] == ' ' || d[i] == ',')) ++i;
    const auto b = i;
    while (i < d.size() && (std::isdigit(static_cast<unsigned char>(d[i])) ||
                            d[i] == '-' || d[i] == '.')) ++i;
    if (b == i) return 0;
    return std::strtof(std::string(d.substr(b, i - b)).c_str(), nullptr);
  };
  auto point = [&]() {
    return ImVec2(org.x + number() * sx * scale, org.y + number() * sy * scale);
  };
  while (i < d.size()) {
    const char c = d[i];
    if (c == 'M' || c == 'L' || c == 'Q' || c == 'Z') {
      cmd = c;
      ++i;
      continue;
    }
    if (cmd == 'M') {
      cur = point();
      start = cur;
      cmd = 'L';  // 后续数字视作 L
    } else if (cmd == 'L') {
      const auto p = point();
      segs.push_back({cur, p});
      cur = p;
    } else if (cmd == 'Q') {
      const auto q = point();  // 控制点
      const auto p = point();  // 终点
      segs.push_back({cur, q});
      segs.push_back({q, p});
      cur = p;
    } else if (cmd == 'Z') {
      segs.push_back({cur, start});
      cur = start;
      cmd = 0;
    } else {
      ++i;
    }
  }
  return segs;
}

}  // namespace

void draw_svg_preview(const SvgScene& scene, ImDrawList* dl,
                      const ImVec2& origin, float scale) {
  if (dl == nullptr || scale <= 0) return;
  const float sx = scale * (scene.width > 0 ? 720.0F / scene.width : 1);
  const float sy = scale * (scene.height > 0 ? 1040.0F / scene.height : 1);

  for (const auto& n : scene.nodes) {
    const auto st = resolve(scene, n);
    const unsigned fill = color_of(st.fill);
    const unsigned stroke = color_of(st.stroke);
    const float sw = st.stroke_width * scale;
    switch (n.kind) {
      case SvgNode::Kind::Rect: {
        const ImVec2 a(origin.x + n.x * sx, origin.y + n.y * sy);
        const ImVec2 b(origin.x + (n.x + n.w) * sx, origin.y + (n.y + n.h) * sy);
        if (fill) dl->AddRectFilled(a, b, fill, n.rx * scale);
        if (stroke) dl->AddRect(a, b, stroke, n.rx * scale, 0, sw);
        break;
      }
      case SvgNode::Kind::Line:
        if (stroke) dl->AddLine(
            ImVec2(origin.x + n.x1 * sx, origin.y + n.y1 * sy),
            ImVec2(origin.x + n.x2 * sx, origin.y + n.y2 * sy), stroke, sw);
        break;
      case SvgNode::Kind::Circle: {
        const ImVec2 c(origin.x + n.cx * sx, origin.y + n.cy * sy);
        if (fill) dl->AddCircleFilled(c, n.r * scale, fill);
        if (stroke) dl->AddCircle(c, n.r * scale, stroke, 0, sw);
        break;
      }
      case SvgNode::Kind::Path: {
        const auto segs = path_segments(n.path_d, sx, sy, 1, origin);
        for (const auto& s : segs)
          if (stroke) dl->AddLine(s.first, s.second, stroke, sw);
        break;
      }
      case SvgNode::Kind::Text: {
        if (!fill || n.text.empty()) break;
        const float fs = st.font_size * scale;
        // writing-mode='vertical-rl'：逐码点沿 y 方向排列
        float y = origin.y + n.y * sy;
        const float x = origin.x + n.x * sx;
        for (const auto& cp : utf8_chars(n.text)) {
          dl->AddText(ImVec2(x, y), fill, std::string(cp).c_str());
          y += st.font_size * sy;  // 与浏览器竖排按字号前进一致
        }
        break;
      }
    }
  }
}

}  // namespace genealogy::gui
