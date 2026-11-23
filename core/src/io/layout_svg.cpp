#include <genealogy/io/layout_svg.hpp>

#include <genealogy/io/layout_data.hpp>
#include <genealogy/io/layout_geometry.hpp>

#include <nlohmann/json.hpp>

#include <genealogy/util/utf8.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

namespace genealogy::io {

namespace {

constexpr int kPageW = 720;
constexpr int kPageH = 1040;

// 列缓冲：同一列里样式相同的连续字符合并为一个 <text>，
// 万级字符页的元素数从「字数」降到「列数 × 样式段数」。
struct Run {
  std::string text;
  std::string style;   // 空=正文，note=夹注，em=专名线
  int half_row_start = 0;
  int half_row_end = 0;
};
struct Column {
  std::vector<Run> runs;
};

std::string esc(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default: out += c;
    }
  }
  return out;
}

bool enabled(const nlohmann::json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_object() && it->value("enabled", false);
}

std::string text_of(const nlohmann::json& j, const char* key,
                    const std::string& fallback = "") {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

}  // namespace

Result<std::string> layout_preset_json(const std::string& id) {
  if (id == "guji_classic") return std::string(layout_data::kGujiClassicJson);
  if (id == "plain_modern") return std::string(layout_data::kPlainModernJson);
  return Error{ErrorCode::InvalidArgument, "未知版式预设: " + id};
}

Result<LayoutGeometry> geometry_for_preset(const std::string& id) {
  auto pj = layout_preset_json(id);
  if (!pj) return pj.error();
  const auto j = nlohmann::json::parse(*pj, nullptr, false);
  if (j.is_discarded())
    return Error{ErrorCode::InvalidArgument, "版式预设 JSON 解析失败: " + id};
  return LayoutGeometry::from_preset(j);
}

Result<std::string> render_layout_page(const LayoutOptions& opt) {
  auto pj = layout_preset_json(opt.preset);
  if (!pj) return pj.error();
  const auto j = nlohmann::json::parse(*pj, nullptr, false);
  if (j.is_discarded())
    return Error{ErrorCode::InvalidArgument, "版式预设 JSON 解析失败: " + opt.preset};

  // 版框区域：天头 60、地脚 55、书口（版心）居中 96 宽
  const int frame_l = 90, frame_r = kPageW - 90;
  const int frame_t = 62, frame_b = kPageH - 62;
  const int center_w = j.value("banxin", nlohmann::json::object()).value("width", "24mm") == "18mm" ? 72 : 96;
  const int center_x = (kPageW - center_w) / 2;
  const bool show_banxin = enabled(j, "banxin");
  const bool show_yuwei = enabled(j, "yuwei");
  const bool show_xiangbi = enabled(j, "xiangbi");
  const bool show_shuer = enabled(j, "shuer");
  const bool show_frame = enabled(j, "frame");
  const bool show_grid = j.value("columns", nlohmann::json::object()).value("grid_rules", false);
  const int col_count = j.value("columns", nlohmann::json::object()).value("count", 10);

  const LayoutGeometry geo = opt.geometry_override.value_or(
      LayoutGeometry::from_preset(j));
  std::ostringstream s;
  s << "<?xml version='1.0' encoding='UTF-8'?>\n"
    << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 "
    << kPageW << " " << kPageH << "' width='100%' role='img'>\n"
    << "<title>" << esc(opt.book_title.empty() ? "家谱" : opt.book_title) << "</title>\n"
    << "<style>\n"
    << "text{font-family:'Source Han Serif SC','Noto Serif CJK SC',serif;fill:#1a1a1a;}\n"
    << ".frame{fill:none;stroke:#222;stroke-width:1.4;}\n"
    << ".frame2{fill:none;stroke:#222;stroke-width:0.6;}\n"
    << ".grid{fill:none;stroke:#b9b0a0;stroke-width:0.5;}\n"
    << ".banxin{fill:none;stroke:#222;stroke-width:0.8;}\n"
    << ".yuwei{fill:none;stroke:#222;stroke-width:1;}\n"
    << ".xiangbi{fill:none;stroke:#222;stroke-width:0.8;}\n"
    << ".body{font-size:" << geo.body_px << "px;writing-mode:vertical-rl;}\n"
    << ".note{font-size:" << geo.note_px << "px;fill:#444;}\n"
    << ".ear{font-size:" << geo.note_px << "px;}\n"
    << ".center{font-size:" << geo.note_px << "px;}\n"
    << "</style>\n"
    << "<rect width='100%' height='100%' fill='#f7f1e3'/>\n";

  // 一、栏线（外框/内框）：四边线型独立可控（左右双边、上下单边等）
  if (show_frame) {
    const auto& fj = j["frame"];
    const std::string rules = text_of(fj, "outer_rules", "double");
    auto side = [&](const char* key) {
      if (!fj.is_object()) return rules;
      const auto it = fj.find("side_rules");
      if (it == fj.end() || !it->is_object()) return rules;
      return text_of(*it, key, rules);
    };
    const std::string top = side("top"), bottom = side("bottom");
    const std::string left = side("left"), right = side("right");
    if (top != "none" && bottom != "none" && left != "none" && right != "none")
      s << "<rect class='frame' x='" << frame_l << "' y='" << frame_t
        << "' width='" << (frame_r - frame_l) << "' height='" << (frame_b - frame_t) << "'/>\n";
    else {
      if (top != "none") s << "<line class='frame' x1='" << frame_l << "' y1='" << frame_t << "' x2='" << frame_r << "' y2='" << frame_t << "'/>\n";
      if (bottom != "none") s << "<line class='frame' x1='" << frame_l << "' y1='" << frame_b << "' x2='" << frame_r << "' y2='" << frame_b << "'/>\n";
      if (left != "none") s << "<line class='frame' x1='" << frame_l << "' y1='" << frame_t << "' x2='" << frame_l << "' y2='" << frame_b << "'/>\n";
      if (right != "none") s << "<line class='frame' x1='" << frame_r << "' y1='" << frame_t << "' x2='" << frame_r << "' y2='" << frame_b << "'/>\n";
    }
    if (rules == "double")
      s << "<rect class='frame2' x='" << (frame_l + 4) << "' y='" << (frame_t + 4)
        << "' width='" << (frame_r - frame_l - 8) << "' height='"
        << (frame_b - frame_t - 8) << "'/>\n";
  }
  // 装订线（四眼/六眼/八眼）：书口侧打孔，公版线装形制
  if (enabled(j, "binding")) {
    const auto& bj = j["binding"];
    const int holes = bj.value("holes", 4);
    const double ratio = bj.value("spacing_ratio", 0.25);
    const int hole_x = frame_l - 28;
    const std::string thread = text_of(bj, "thread_color", "#444");
    s << "<line x1='" << hole_x << "' y1='" << frame_t << "' x2='" << hole_x
      << "' y2='" << frame_b << "' stroke='" << thread << "' stroke-width='0.6' stroke-dasharray='2 3'/>\n";
    for (int i = 0; i < holes; ++i) {
      const int hy = static_cast<int>(frame_t + (frame_b - frame_t) *
          (ratio + (1 - 2 * ratio) * i / std::max(1, holes - 1)));
      s << "<circle cx='" << hole_x << "' cy='" << hy << "' r='3' fill='#f7f1e3' stroke='"
        << thread << "' stroke-width='0.8'/>\n";
    }
  }
  // 二、界行（column grid rules）：自左向右均分，版心处留空
  if (show_grid) {
    const int usable_l = frame_l + 14, usable_r = center_x - 10;
    const int count = std::max(2, col_count / 2);
    for (int i = 0; i <= count; ++i) {
      const int x = usable_l + (usable_r - usable_l) * i / count;
      s << "<line class='grid' x1='" << x << "' y1='" << (frame_t + 10)
        << "' x2='" << x << "' y2='" << (frame_b - 10) << "'/>\n";
    }
    for (int i = 0; i <= count; ++i) {
      const int x = center_x + center_w + 10 +
                    (frame_r - 14 - center_x - center_w - 10) * i / count;
      s << "<line class='grid' x1='" << x << "' y1='" << (frame_t + 10)
        << "' x2='" << x << "' y2='" << (frame_b - 10) << "'/>\n";
    }
  }

  // 三、版心（中缝）：书名 / 卷次 / 页码
  if (show_banxin) {
    s << "<rect class='banxin' x='" << center_x << "' y='" << frame_t
      << "' width='" << center_w << "' height='" << (frame_b - frame_t) << "'/>\n";
    if (show_xiangbi) {
      // 象鼻：单线/双线由预设 line_count 控制（自绘几何，非任何软件图样）
      const int nline = std::max(1, j["xiangbi"].value("line_count", 2));
      const int gap = nline > 1 ? 5 : 0;
      for (int k = 0; k < nline; ++k) {
        const int off = k * gap;
        s << "<line class='xiangbi' x1='" << center_x << "' y1='" << (frame_t + 34 + off)
          << "' x2='" << (center_x + center_w) << "' y2='" << (frame_t + 34 + off) << "'/>\n"
          << "<line class='xiangbi' x1='" << center_x << "' y1='" << (frame_b - 34 - off)
          << "' x2='" << (center_x + center_w) << "' y2='" << (frame_b - 34 - off) << "'/>\n";
      }
    }
    if (show_yuwei) {
      // 鱼尾：五种自绘形制，由预设 style/count 控制。
      // 几何为公版古籍版式通识的简化表达，不描摹任何软件素材。
      const auto& yj = j["yuwei"];
      const std::string ystyle = text_of(yj, "style", "single_black");
      const int ycount = std::max(1, yj.value("count", 1));
      const int cx = center_x + center_w / 2;
      const double upper = yj.value("upper_y_ratio", 0.08);
      const double lower = yj.value("lower_y_ratio", 0.92);
      const int top_y = static_cast<int>(frame_t + (frame_b - frame_t) * upper);
      const int bot_y = static_cast<int>(frame_t + (frame_b - frame_t) * lower);
      auto draw_tail = [&](int yy, bool up) {
        if (ystyle == "flower") {
          // 花瓣鱼尾：三个圆弧瓣
          s << "<path class='yuwei' d='M" << (cx - 24) << " " << yy
            << " Q" << (cx - 12) << " " << (yy + (up ? 16 : -16)) << " " << cx << " " << yy
            << " Q" << (cx + 12) << " " << (yy + (up ? 16 : -16)) << " " << (cx + 24) << " " << yy
            << " Z'/>\n";
        } else if (ystyle == "cloud") {
          // 云头鱼尾：波浪曲线
          s << "<path class='yuwei' d='M" << (cx - 26) << " " << yy
            << " Q" << (cx - 13) << " " << (yy - 12) << " " << cx << " " << yy
            << " Q" << (cx + 13) << " " << (yy - 12) << " " << (cx + 26) << " " << yy
            << " L" << cx << " " << (yy + (up ? 18 : -18)) << " Z'/>\n";
        } else if (ystyle == "line") {
          // 线鱼尾：仅横线，无三角
          s << "<line class='yuwei' x1='" << (cx - 22) << "' y1='" << yy
            << "' x2='" << (cx + 22) << "' y2='" << yy << "'/>\n";
        } else {
          // single_black / double_black：实心三角 + 横线
          s << "<path class='yuwei' d='M" << (cx - 26) << " " << yy
            << " L" << (cx + 26) << " " << yy << " L" << cx << " " << (yy + (up ? 18 : -18))
            << " Z'/>\n"
            << "<line class='yuwei' x1='" << (cx - 30) << "' y1='" << (yy - 3)
            << "' x2='" << (cx + 30) << "' y2='" << (yy - 3) << "'/>\n";
        }
      };
      draw_tail(top_y, true);
      if (ycount > 1) draw_tail(bot_y, false);
    }
    const int cy = (frame_t + frame_b) / 2;
    if (!opt.volume_title.empty())
      s << "<text class='center' x='" << (center_x + center_w / 2) << "' y='"
        << (cy - 14) << "' text-anchor='middle'>" << esc(opt.volume_title) << "</text>\n";
    if (!opt.book_title.empty())
      s << "<text class='center' x='" << (center_x + center_w / 2) << "' y='"
        << cy << "' text-anchor='middle'>" << esc(opt.book_title) << "</text>\n";
    s << "<text class='center' x='" << (center_x + center_w / 2) << "' y='"
      << (cy + 22) << "' text-anchor='middle'>" << opt.page_number << "/" << opt.total_pages
      << "</text>\n";
  }

  // 四、书耳（左上角耳形小框）
  if (show_shuer) {
    s << "<rect class='banxin' x='" << (frame_l - 56) << "' y='" << (frame_t + 20)
      << "' width='52' height='30' rx='4'/>\n";
    s << "<text class='ear' x='" << (frame_l - 30) << "' y='" << (frame_t + 40)
      << "' text-anchor='middle'>" << esc(opt.volume_title.empty() ? "卷" : opt.volume_title)
      << "</text>\n";
  }

  // 五、正文：分页器与渲染器共用 TextCursor 几何模型。
  // 每个字符的真实 (列, 半行) 由游标给出；同列同样式连续字符合并为一个
  // <text>。分页器已用同一游标切页，故本页内容必然落在本页版框内。
  TextCursor cursor(geo);
  std::vector<Column> columns(static_cast<std::size_t>(columns_per_page(geo)));
  // 专名线字符的逻辑位置序列（按输入顺序），用于绘制跨列连续下划线
  std::vector<TextCursor::Placement> em_marks;

  auto emit_char = [&](const std::string_view cp, const char* style) {
    const bool note = std::string(style) == "note" || std::string(style) == "cont";
    TextCursor::Placement p;
    if (!cursor.place(note, p)) return;  // 本页已满（分页器已保证不会发生）
    auto& col = columns[static_cast<std::size_t>(p.col)];
    if (col.runs.empty() || col.runs.back().half_row_end != p.half_row ||
        col.runs.back().style != style) {
      Run r;
      r.style = style;
      r.half_row_start = p.half_row;
      r.half_row_end = p.half_row;
      col.runs.push_back(r);
    }
    col.runs.back().text.append(cp);
    col.runs.back().half_row_end = p.half_row + (note ? 1 : 2);
    if (std::string(style) == "em") em_marks.push_back(p);
  };

  for (const auto& n : opt.content) {
    switch (n.kind) {
      case MarkupKind::Volume:
      case MarkupKind::Chapter:
      case MarkupKind::Section:
        for (const auto& cp : utf8_chars(n.text)) emit_char(cp, "title");
        break;
      case MarkupKind::Text:
        for (const auto& cp : utf8_chars(n.text)) emit_char(cp, "");
        break;
      case MarkupKind::Note:
      case MarkupKind::Marginal:
      case MarkupKind::Ear:
        for (const auto& cp : utf8_chars(n.text)) emit_char(cp, "note");
        break;
      case MarkupKind::Continuation:
        for (const auto& cp : utf8_chars(n.text)) emit_char(cp, "cont");
        break;
      case MarkupKind::Emphasis:
        for (const auto& cp : utf8_chars(n.text)) emit_char(cp, "em");
        break;
      case MarkupKind::Circle:
        emit_char("○", "dot");
        break;
      case MarkupKind::Dot:
        emit_char("、", "dot");
        break;
      case MarkupKind::Indent:
        for (int i = 0; i < n.value; ++i) emit_char("　", "");
        break;
      case MarkupKind::PageBreak:
        break;
    }
  }

  // 输出：每个 Run 一个 <text>，SVG 原生竖排；坐标由共享几何算出。
  for (int col_idx = 0; col_idx < columns_per_page(geo); ++col_idx) {
    const auto& col = columns[static_cast<std::size_t>(col_idx)];
    const int x = column_x(geo, col_idx);
    if (x < geo.center_x + geo.center_w + 8) break;
    for (const auto& run : col.runs) {
      const char* cls = run.style == "note" ? "note"
                       : run.style == "cont" ? "warn"
                       : "body";
      std::string extra;
      if (run.style == "title")
        extra = " style='font-size:" + std::to_string(
            static_cast<int>(std::lround(geo.body_px * 1.27))) + "px;font-weight:500'";
      if (run.style == "cont") extra = " fill='#a32d2d'";
      const int y = row_y(geo, run.half_row_start);
      s << "<text class='" << cls << "' x='" << x << "' y='" << y
        << "' writing-mode='vertical-rl' glyph-orientation-vertical='0'" << extra << ">"
        << esc(run.text) << "</text>\n";
    }
  }
  // 专名线覆盖层：同列连续字符合并为一条长竖线，跨列折线连接。
  // 比例从字号推导（mark_dx / 竖线上下伸出量），字号改变自动适配。
  if (!em_marks.empty()) {
    const int mark_dx = geo.mark_dx;
    const int over = std::max(2, static_cast<int>(std::lround(geo.body_px * 0.8)));
    const int under = std::max(2, static_cast<int>(std::lround(geo.body_px * 0.27)));
    std::string path_d;
    char buf[80];
    // 按列切段：同列内合并首末为一条线
    std::size_t i = 0;
    while (i < em_marks.size()) {
      const int col = em_marks[i].col;
      std::size_t j = i + 1;
      while (j < em_marks.size() && em_marks[j].col == col) ++j;
      const int x = column_x(geo, col) + mark_dx;
      const int y1 = row_y(geo, em_marks[i].half_row) - over;
      const int y2 = row_y(geo, em_marks[j - 1].half_row) + under;
      std::snprintf(buf, sizeof(buf), "M%d %d L%d %d ", x, y1, x, y2);
      path_d += buf;
      // 与本段末字符到下一列首字符之间补折线（跨列连续）
      if (j < em_marks.size() && em_marks[j].col != col) {
        const int nx = column_x(geo, em_marks[j].col) + mark_dx;
        const int ny = row_y(geo, em_marks[j].half_row) - over;
        std::snprintf(buf, sizeof(buf), "M%d %d L%d %d ", x, y2, nx, ny);
        path_d += buf;
      }
      i = j;
    }
    s << "<path d='" << path_d << "' fill='none' stroke='#a32d2d' stroke-width='0.9'/>" << "\n";
  }

  s << "</svg>\n";
  return s.str();
}
}  // namespace genealogy::io
