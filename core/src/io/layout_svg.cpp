#include <genealogy/io/layout_svg.hpp>

#include <genealogy/io/layout_data.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <string>

namespace genealogy::io {

namespace {

constexpr int kPageW = 720;
constexpr int kPageH = 1040;

std::string esc(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

// UTF-8 码点切分：中文 3 字节，逐 char 遍历会切出非法字节序列
// （与标记解析的〔〕偏移坑同源）。
std::vector<std::string> utf8_chars(const std::string& s) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t n = 1;
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    out.push_back(s.substr(i, std::min(n, s.size() - i)));
    i += n;
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
    << ".body{font-size:15px;writing-mode:vertical-rl;}\n"
    << ".note{font-size:10px;fill:#444;}\n"
    << ".ear{font-size:11px;}\n"
    << ".center{font-size:12px;}\n"
    << "</style>\n"
    << "<rect width='100%' height='100%' fill='#f7f1e3'/>\n";

  // 一、栏线（外框/内框）
  if (show_frame) {
    const std::string rules = text_of(j["frame"], "outer_rules", "double");
    s << "<rect class='frame' x='" << frame_l << "' y='" << frame_t
      << "' width='" << (frame_r - frame_l) << "' height='" << (frame_b - frame_t) << "'/>\n";
    if (rules == "double")
      s << "<rect class='frame2' x='" << (frame_l + 4) << "' y='" << (frame_t + 4)
        << "' width='" << (frame_r - frame_l - 8) << "' height='"
        << (frame_b - frame_t - 8) << "'/>\n";
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
      // 象鼻：版心上下两段horizon线（自绘，非任何软件图样）
      s << "<line class='xiangbi' x1='" << center_x << "' y1='" << (frame_t + 34)
        << "' x2='" << (center_x + center_w) << "' y2='" << (frame_t + 34) << "'/>\n"
        << "<line class='xiangbi' x1='" << center_x << "' y1='" << (frame_b - 34)
        << "' x2='" << (center_x + center_w) << "' y2='" << (frame_b - 34) << "'/>\n";
    }
    if (show_yuwei) {
      // 鱼尾：倒三角 + 横线（自行设计的几何形制）
      const int yw_y = frame_t + 26, cx = center_x + center_w / 2;
      s << "<path class='yuwei' d='M" << (cx - 26) << " " << yw_y
        << " L" << (cx + 26) << " " << yw_y << " L" << cx << " " << (yw_y + 18)
        << " Z'/>\n"
        << "<line class='yuwei' x1='" << (cx - 30) << "' y1='" << (yw_y - 3)
        << "' x2='" << (cx + 30) << "' y2='" << (yw_y - 3) << "'/>\n";
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

  // 五、正文（按标记节点渲染；竖排由 CSS writing-mode 实现）
  int x = frame_r - 24;
  int y = frame_t + 30;
  for (const auto& n : opt.content) {
    switch (n.kind) {
      case MarkupKind::Volume:
      case MarkupKind::Chapter:
      case MarkupKind::Section: {
        s << "<text class='body' x='" << x << "' y='" << y
          << "' style='font-size:19px;font-weight:500'>" << esc(n.text) << "</text>\n";
        x -= 30;
        break;
      }
      case MarkupKind::Text: {
        // 逐字竖排：一个字一行，到达底部换列
        for (const auto& cp : utf8_chars(n.text)) {
          if (y > frame_b - 20) { y = frame_t + 30; x -= 22; }
          if (x < center_x + center_w + 20) break;
          s << "<text class='body' x='" << x << "' y='" << y << "'>"
            << esc(cp) << "</text>\n";
          y += 19;
        }
        break;
      }
      case MarkupKind::Note:
      case MarkupKind::Marginal:
      case MarkupKind::Ear: {
        s << "<text class='note' x='" << (x - 6) << "' y='" << y << "'>"
          << esc(n.text) << "</text>\n";
        y += 14;
        break;
      }
      case MarkupKind::Circle:
        s << "<circle cx='" << (x - 12) << "' cy='" << (y - 5)
          << "' r='4' fill='none' stroke='#a32d2d' stroke-width='1'/>\n";
        break;
      case MarkupKind::Dot:
        s << "<circle cx='" << (x - 12) << "' cy='" << (y - 5)
          << "' r='2' fill='#a32d2d'/>\n";
        break;
      case MarkupKind::Emphasis: {
        for (const auto& cp : utf8_chars(n.text)) {
          if (y > frame_b - 20) { y = frame_t + 30; x -= 22; }
          s << "<text class='body' x='" << x << "' y='" << y << "'>"
            << esc(cp) << "</text>\n"
            << "<line x1='" << (x + 14) << "' y1='" << (y - 12)
            << "' x2='" << (x + 14) << "' y2='" << (y + 2)
            << "' stroke='#a32d2d' stroke-width='0.8'/>\n";
          y += 19;
        }
        break;
      }
      case MarkupKind::PageBreak:
        x = frame_r - 24;
        y = frame_t + 30;
        break;
      case MarkupKind::Indent:
        y += n.value * 19;
        break;
    }
  }
  s << "</svg>\n";
  return s.str();
}

}  // namespace genealogy::io
