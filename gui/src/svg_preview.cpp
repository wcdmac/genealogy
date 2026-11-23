// 自家 SVG 子集的解析器（纯 C++，无 ImGui 依赖，可无头测试）。

#include <genealogy/gui/svg_preview.hpp>

#include <algorithm>
#include <cstdlib>

namespace genealogy::gui {

namespace {

std::string_view trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

float num(std::string_view s, float fallback = 0) {
  if (s.empty()) return fallback;
  try {
    return std::strtof(std::string(s).c_str(), nullptr);
  } catch (...) {
    return fallback;
  }
}

// 解析开标签的属性串：attr='value' 或 attr="value"
std::map<std::string, std::string> attrs(std::string_view tag) {
  std::map<std::string, std::string> out;
  std::size_t i = 0;
  while (i < tag.size()) {
    while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\n')) ++i;
    const std::size_t ke = tag.find_first_of("= \t\r\n/>", i);
    if (ke == std::string_view::npos) break;
    std::string key(tag.substr(i, ke - i));
    if (key.empty()) { ++i; continue; }
    std::size_t eq = ke;
    while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
    if (eq >= tag.size() || tag[eq] != '=') { i = ke + 1; continue; }
    ++eq;
    while (eq < tag.size() && (tag[eq] == ' ' || tag[eq] == '\t')) ++eq;
    if (eq >= tag.size() || (tag[eq] != '\'' && tag[eq] != '"')) { i = eq; continue; }
    const char quote = tag[eq++];
    const std::size_t ve = tag.find(quote, eq);
    if (ve == std::string_view::npos) break;
    out[key] = std::string(tag.substr(eq, ve - eq));
    i = ve + 1;
  }
  return out;
}

// 解析 `<style>` 里的 .cls{prop:val;...}，以及无点的全局 text{} 规则
void parse_styles(std::string_view css, SvgScene& scene) {
  std::size_t pos = 0;
  while (pos < css.size()) {
    const auto lb = css.find('{', pos);
    if (lb == std::string_view::npos) break;
    const auto rb = css.find('}', lb + 1);
    if (rb == std::string_view::npos) break;
    std::string selector(trim(css.substr(pos, lb - pos)));
    std::string body(css.substr(lb + 1, rb - lb - 1));
    std::string cls = selector.empty() ? "" :
        (selector[0] == '.' ? selector.substr(1) : selector);
    SvgStyle st;
    std::size_t p = 0;
    while (p < body.size()) {
      const auto colon = body.find(':', p);
      if (colon == std::string::npos) break;
      const auto semi = body.find(';', colon + 1);
      const auto end = semi == std::string::npos ? body.size() : semi;
      const std::string prop(trim(std::string_view(body).substr(p, colon - p)));
      const std::string val(trim(std::string_view(body).substr(colon + 1, end - colon - 1)));
      if (prop == "fill") st.fill = val;
      else if (prop == "stroke") st.stroke = val;
      else if (prop == "stroke-width") {
        st.stroke_width = num(val, 1);
      } else if (prop == "font-size") {
        std::string v = val;
        if (!v.empty() && v.back() == 'x') v.pop_back();
        if (!v.empty() && v.back() == 'p') v.pop_back();
        st.font_size = num(v, 15);
      }
      p = (semi == std::string::npos) ? body.size() : semi + 1;
    }
    scene.classes[cls] = st;
    pos = rb + 1;
  }
}

std::string unescape(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '&') {
      if (s.substr(i, 5) == "&amp;") { out += '&'; i += 4; continue; }
      if (s.substr(i, 4) == "&lt;") { out += '<'; i += 3; continue; }
      if (s.substr(i, 4) == "&gt;") { out += '>'; i += 3; continue; }
      if (s.substr(i, 6) == "&quot;") { out += '"'; i += 5; continue; }
    }
    out += s[i];
  }
  return out;
}

void apply_inline(const std::map<std::string, std::string>& a, SvgNode& n) {
  auto it = a.find("class");
  if (it != a.end()) n.cls = it->second;
  it = a.find("fill");
  if (it != a.end()) { n.fill = it->second; n.has_fill = true; }
  it = a.find("stroke");
  if (it != a.end()) { n.stroke = it->second; n.has_stroke = true; }
  it = a.find("stroke-width");
  if (it != a.end()) n.stroke_width = num(it->second);
  it = a.find("font-size");
  if (it != a.end()) {
    std::string v = it->second;
    if (!v.empty() && v.back() == 'x') v.pop_back();
    n.font_size = num(v);
  }
  // 内联 style='font-size:19px;...'：预览只需 font-size
  it = a.find("style");
  if (it != a.end()) {
    const std::string& st = it->second;
    const auto k = st.find("font-size:");
    if (k != std::string::npos) {
      const auto e = st.find(';', k);
      std::string v = st.substr(k + 10, e == std::string::npos ?
          std::string::npos : e - k - 10);
      if (!v.empty() && v.back() == 'x') v.pop_back();
      n.font_size = num(v);
    }
  }
}

}  // namespace

Result<SvgScene> parse_own_svg(std::string_view svg) {
  SvgScene scene;
  // viewBox="0 0 720 1040"
  const auto vb = svg.find("viewBox");
  if (vb != std::string_view::npos) {
    const auto q1 = svg.find_first_of("'\"", vb);
    if (q1 != std::string_view::npos) {
      const char quote = svg[q1];
      const auto q2 = svg.find(quote, q1 + 1);
      if (q2 != std::string_view::npos) {
        const auto vals = attrs("a='" + std::string(svg.substr(q1 + 1, q2 - q1 - 1)) + "'");
        // 直接按空格拆
        std::string box(svg.substr(q1 + 1, q2 - q1 - 1));
        int nums[4] = {0, 0, 720, 1040};
        std::size_t p = 0, k = 0;
        while (p < box.size() && k < 4) {
          while (p < box.size() && box[p] == ' ') ++p;
          const auto start = p;
          while (p < box.size() && box[p] != ' ') ++p;
          if (start < p) nums[k++] = std::atoi(box.substr(start, p - start).c_str());
        }
        scene.width = static_cast<float>(nums[2]);
        scene.height = static_cast<float>(nums[3]);
      }
    }
  }
  // <style>...</style>
  const auto sb = svg.find("<style>");
  const auto se = svg.find("</style>");
  if (sb != std::string_view::npos && se != std::string_view::npos)
    parse_styles(svg.substr(sb + 7, se - sb - 7), scene);

  static const char* kTags[] = {"rect", "line", "circle", "path", "text"};
  std::size_t pos = 0;
  while (pos < svg.size()) {
    const auto lt = svg.find('<', pos);
    if (lt == std::string_view::npos) break;
    std::size_t which = 5;
    for (std::size_t k = 0; k < 5; ++k) {
      const std::string prefix = "<" + std::string(kTags[k]);
      if (svg.compare(lt, prefix.size(), prefix) == 0) { which = k; break; }
    }
    if (which == 5) { pos = lt + 1; continue; }
    const auto gt = svg.find('>', lt);
    if (gt == std::string_view::npos) break;
    const auto a = attrs(svg.substr(lt + 1, gt - lt - 1));
    SvgNode n;
    switch (which) {
      case 0: n.kind = SvgNode::Kind::Rect; break;
      case 1: n.kind = SvgNode::Kind::Line; break;
      case 2: n.kind = SvgNode::Kind::Circle; break;
      case 3: n.kind = SvgNode::Kind::Path; break;
      default: n.kind = SvgNode::Kind::Text; break;
    }
    apply_inline(a, n);
    auto getf = [&](const char* key, float def = 0) {
      auto it = a.find(key);
      return it == a.end() ? def : num(it->second, def);
    };
    switch (n.kind) {
      case SvgNode::Kind::Rect:
        n.x = getf("x"); n.y = getf("y");
        n.w = getf("width"); n.h = getf("height");
        n.rx = getf("rx");
        break;
      case SvgNode::Kind::Line:
        n.x1 = getf("x1"); n.y1 = getf("y1");
        n.x2 = getf("x2"); n.y2 = getf("y2");
        break;
      case SvgNode::Kind::Circle:
        n.cx = getf("cx"); n.cy = getf("cy"); n.r = getf("r");
        break;
      case SvgNode::Kind::Path: {
        auto it = a.find("d");
        if (it != a.end()) n.path_d = it->second;
        break;
      }
      case SvgNode::Kind::Text: {
        n.x = getf("x"); n.y = getf("y");
        const auto close = svg.find("</text>", gt + 1);
        if (close != std::string_view::npos) {
          n.text = unescape(svg.substr(gt + 1, close - gt - 1));
          pos = close + 7;
          scene.nodes.push_back(n);
          continue;
        }
        break;
      }
    }
    scene.nodes.push_back(n);
    pos = gt + 1;
  }
  return scene;
}

std::size_t svg_node_count(const SvgScene& scene) { return scene.nodes.size(); }
std::size_t svg_class_count(const SvgScene& scene) { return scene.classes.size(); }

}  // namespace genealogy::gui
