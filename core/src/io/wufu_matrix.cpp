#include <genealogy/io/wufu_matrix.hpp>

#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>

#include <algorithm>
#include <map>
#include <sstream>
#include <unordered_set>

namespace genealogy::io {

namespace {

const char* kGrades[] = {"斩衰", "齐衰", "大功", "小功", "缌麻"};

int column_of(lineage::MourningGrade g) {
  using G = lineage::MourningGrade;
  switch (g) {
    case G::ZhanCui: return 0;
    case G::QiCuiZhang:
    case G::QiCuiBuZhang:
    case G::QiCuiWuYue:
    case G::QiCuiSanYue: return 1;
    case G::DaGong: return 2;
    case G::XiaoGong: return 3;
    case G::SiMa: return 4;
    default: return -1;  // 袒免/无服不入五服矩阵
  }
}

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

}  // namespace

Result<ChartResult> render_wufu_matrix(const ChartOptions& opt) {
  if (!opt.graph) return Error{ErrorCode::InvalidArgument, "五服矩阵缺少人物图"};
  const auto& g = *opt.graph;
  if (!g.find(opt.focus_id))
    return Error{ErrorCode::NotFound, "五服矩阵焦点人物不存在: " + opt.focus_id};

  // 九族纵轴：从上四世到下四世（generation_gap = -4..+4）；五服横轴 5 列。
  struct Cell {
    int count = 0;
    std::vector<std::string> names;
  };
  std::map<int, std::array<Cell, 5>> grid;
  for (int i = -4; i <= 4; ++i) grid[i];

  std::size_t examined = 0;
  std::vector<std::string> warnings;
  for (const auto& kv : g.nodes()) {
    if (kv.first == opt.focus_id) continue;
    const auto rel = lineage::find_relationship(g, opt.focus_id, kv.first, 12);
    if (!rel.found) continue;
    const int gap = rel.generation_gap();
    if (gap < -4 || gap > 4) continue;
    auto m = lineage::compute_mourning_degree(
        g, {opt.focus_id, kv.first, "ming_qing"});
    if (!m) continue;
    int col = column_of(m->grade);
    if (col < 0) continue;  // 无服者不入矩阵
    ++examined;
    auto& cell = grid[gap][col];
    ++cell.count;
    if (cell.names.size() < 4)
      cell.names.push_back(kv.second->family_name + kv.second->given_name);
  }
  if (examined == 0)
    warnings.push_back("焦点人物在图中没有可计算的五服亲属");

  const std::string title = opt.title.empty() ? "九族五服图" : opt.title;
  const int cw = 150, rh = 52, left = 120, top = 90;
  const int W = left + 5 * cw + 40;
  const int H = top + 9 * rh + 80;

  std::ostringstream s;
  s << "<?xml version='1.0' encoding='UTF-8'?>\n"
    << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " << W << " "
    << H << "' width='100%' role='img'>\n"
    << "<title>" << esc(title) << "</title>\n"
    << "<style>\n"
    << "text{font-family:'Source Han Sans SC','Noto Sans CJK SC',sans-serif;}\n"
    << ".h{font-size:15px;fill:#222;} .th{font-size:13px;fill:#333;}\n"
    << ".cell{font-size:11px;fill:#111;} .n{font-size:11px;fill:#666;}\n"
    << ".grid{fill:none;stroke:#888;stroke-width:1;}\n"
    << ".ego{fill:#f2e8d5;} .warn{fill:#a32d2d;font-size:11px;}\n"
    << "</style>\n"
    << "<text class='h' x='30' y='36'>" << esc(title)
    << "（焦点：" << esc(g.find(opt.focus_id)->family_name +
                         g.find(opt.focus_id)->given_name)
    << "）</text>\n";

  // 表头：五服等级
  for (int c = 0; c < 5; ++c) {
    s << "<text class='th' x='" << (left + c * cw + cw / 2) << "' y='"
      << (top - 20) << "' text-anchor='middle'>" << kGrades[c] << "</text>\n";
  }
  const char* gens[] = {"高祖辈", "曾祖辈", "祖辈", "父辈", "己身",
                        "子辈", "孙辈", "曾孙辈", "玄孙辈"};
  for (int r = 0; r < 9; ++r) {
    const int gap = r - 4;
    const int y = top + r * rh;
    s << "<text class='th' x='" << (left - 12) << "' y='" << (y + rh / 2 + 4)
      << "' text-anchor='end'>" << gens[r] << "</text>\n";
    for (int c = 0; c < 5; ++c) {
      const auto& cell = grid[gap][c];
      if (gap == 0)
        s << "<rect class='ego' x='" << (left + c * cw) << "' y='" << y
          << "' width='" << cw << "' height='" << rh << "'/>\n";
      s << "<rect class='grid' x='" << (left + c * cw) << "' y='" << y
        << "' width='" << cw << "' height='" << rh << "'/>\n";
      if (cell.count) {
        s << "<text class='cell' x='" << (left + c * cw + 6) << "' y='"
          << (y + 16) << "'>" << cell.count << " 人</text>\n";
        std::string names;
        for (std::size_t i = 0; i < cell.names.size(); ++i) {
          if (i) names += "、";
          names += cell.names[i];
        }
        s << "<text class='n' x='" << (left + c * cw + 6) << "' y='"
          << (y + 36) << "'>" << esc(names) << "</text>\n";
      }
    }
  }
  for (std::size_t i = 0; i < warnings.size() && i < 6; ++i)
    s << "<text class='warn' x='30' y='" << (top + 9 * rh + 20 + i * 15)
      << "'>" << esc(warnings[i]) << "</text>\n";
  s << "<text class='n' x='30' y='" << (H - 16)
    << "'>服等由外置规则表 ming_qing 逐对计算；袒免/无服不入五服格。</text>\n"
    << "</svg>\n";

  ChartResult out;
  out.svg = s.str();
  out.pages.push_back({out.svg, -4, 4, false});
  out.node_count = examined;
  out.page_count = 1;
  out.warnings = warnings;
  return out;
}

}  // namespace genealogy::io
