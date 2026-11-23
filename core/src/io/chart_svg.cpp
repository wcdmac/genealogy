#include <genealogy/io/chart_svg.hpp>
#include <genealogy/io/wufu_matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace genealogy::io {

namespace {

constexpr int kNodeW = 96;
constexpr int kNodeH = 34;
constexpr int kGapX = 18;
constexpr int kGapY = 64;
constexpr int kMargin = 40;

std::string esc(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
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

std::string label_of(const Person& p) {
  return p.family_name + p.given_name;
}

std::string life_of(const Person& p) {
  if (p.birth_date.gregorian) return *p.birth_date.gregorian;
  if (p.birth_date.lunar) return *p.birth_date.lunar;
  return "";
}

// 布局节点：绝对坐标 + 世代 + 房支（用始祖后第一子分支作分色键）
struct laid_out {
  const Person* person = nullptr;
  double x = 0, y = 0;
  int gen = 0;
  std::string branch;
};

struct layout {
  std::vector<laid_out> nodes;
  std::vector<std::pair<std::size_t, std::size_t>> edges;
  std::size_t focus_index = 0;
};

std::string branch_key(const lineage::PersonGraph& g, const Person& p) {
  std::string branch = p.id;
  const Person* cur = &p;
  for (int i = 0; i < 40; ++i) {
    const Person* f = g.father_of(cur->id);
    if (!f) break;
    cur = f;
    branch = f->id;
  }
  return branch;
}

// 后代分层布局：层内按 children_of 的顺序（行几→出生→id），父居中于子。
layout descendants(const lineage::PersonGraph& g, const std::string& root,
                   int depth, bool with_spouses,
                   std::vector<std::string>* warnings) {
  layout out;
  const Person* r = g.find(root);
  if (!r) return out;
  std::unordered_map<std::string, std::size_t> index;
  auto add = [&](const Person* p, int gen, double x) -> std::size_t {
    const auto it = index.find(p->id);
    if (it != index.end()) return it->second;
    const std::size_t i = out.nodes.size();
    index[p->id] = i;
    out.nodes.push_back({p, x, static_cast<double>(kMargin + gen * kGapY), gen, branch_key(g, *p)});
    return i;
  };
  const std::size_t ri = add(r, 0, 0);
  out.focus_index = ri;

  std::unordered_set<std::string> visited;
  std::vector<std::size_t> frontier{ri};
  for (int gen = 0; gen < depth; ++gen) {
    std::vector<std::size_t> next;
    // 先收集本层所有父的子，再统一排序定位（避免父子互相覆盖）。
    // 配偶必须与子女同层参与 x 分配——否则 x 恒为 0，与首个子女重叠。
    std::vector<std::vector<std::size_t>> kid_groups;
    std::vector<std::vector<std::size_t>> spouse_groups;
    for (std::size_t pi : frontier) {
      const auto& kids = g.children_of(out.nodes[pi].person->id);
      std::vector<std::size_t> group;
      for (const Person* c : kids) {
        if (!visited.insert(c->id).second) {
          // 双亲子女（同一孩子被父母双方引用）是正常谱面；只有回指
          // 祖先（孩子世代 <= 父辈世代）才是真环。
          const auto parent_gen =
              out.nodes[pi].person->generation_no.value_or(0);
          const auto child_gen = c->generation_no.value_or(0);
          if (child_gen != 0 && parent_gen != 0 && child_gen <= parent_gen) {
            warnings->push_back("检测到成环亲子关系: " + c->id);
          }
          continue;
        }
        group.push_back(add(c, gen + 1, 0));
      }
      kid_groups.push_back(group);
      std::vector<std::size_t> sgroup;
      if (with_spouses) {
        for (const Person* sp : g.spouses_of(out.nodes[pi].person->id)) {
          const std::size_t si = add(sp, gen, 0);
          sgroup.push_back(si);
          out.edges.emplace_back(pi, si);
        }
      }
      spouse_groups.push_back(sgroup);
    }
    // 按组顺序把每层节点依次排开：先配偶（与本代主干同 gen），再子女。
    // 每个节点独占 kNodeW+kGapX，保证同层不重叠。
    double cursor = kMargin;
    for (std::size_t gi = 0; gi < kid_groups.size(); ++gi) {
      for (std::size_t si : spouse_groups[gi]) {
        out.nodes[si].x = cursor;
        cursor += kNodeW + kGapX;
      }
      for (std::size_t ci : kid_groups[gi]) {
        out.nodes[ci].x = cursor;
        cursor += kNodeW + kGapX;
        if (gi < frontier.size()) out.edges.emplace_back(frontier[gi], ci);
      }
    }
    for (std::size_t pi : frontier) {
      const auto& kids = g.children_of(out.nodes[pi].person->id);
      if (kids.empty()) continue;
      double sum = 0;
      int n = 0;
      for (const Person* c : kids) {
        const auto it = index.find(c->id);
        if (it != index.end() && out.nodes[it->second].gen == gen + 1) {
          sum += out.nodes[it->second].x;
          ++n;
        }
      }
      if (n) out.nodes[pi].x = sum / n;
    }
    next.clear();
    for (auto& group : kid_groups)
      for (std::size_t ci : group) next.push_back(ci);
    if (next.empty()) break;
    frontier = next;
  }
  // 根节点若被居中后落到负数，整体右移
  double minx = 0;
  for (const auto& n : out.nodes) minx = std::min(minx, n.x);
  if (minx < 0)
    for (auto& n : out.nodes) n.x -= minx;
  return out;
}

// 祖先图：每层最多 2 位生父生母，位置固定（可预测网格）。
layout pedigree(const lineage::PersonGraph& g, const std::string& root,
                int depth, std::vector<std::string>* warnings) {
  layout out;
  const Person* r = g.find(root);
  if (!r) return out;
  std::unordered_map<std::string, std::size_t> index;
  auto add = [&](const Person* p, int gen, double x) -> std::size_t {
    const auto it = index.find(p->id);
    if (it != index.end()) return it->second;
    const std::size_t i = out.nodes.size();
    index[p->id] = i;
    out.nodes.push_back({p, x, static_cast<double>(kMargin + gen * kGapY), gen, branch_key(g, *p)});
    return i;
  };
  const std::size_t ri = add(r, 0, 0);
  out.focus_index = ri;
  std::vector<std::size_t> frontier{ri};
  for (int gen = 0; gen < depth; ++gen) {
    std::vector<std::size_t> next;
    for (std::size_t pi : frontier) {
      const Person* cur = out.nodes[pi].person;
      const Person* f = g.father_of(cur->id);
      const Person* m = g.mother_of(cur->id);
      int k = 0;
      for (const Person* p : {f, m}) {
        if (!p) {
          // 空框：显式展示缺失的祖先，pedigree 的核心价值
          warnings->push_back("祖先缺失: " + cur->id + " 第 " + std::to_string(gen + 1) + " 代");
          continue;
        }
        const std::size_t ci = add(p, gen + 1, out.nodes[pi].x + (k++ ? 1 : -1) * (kNodeW + kGapX) / 2.0);
        out.edges.emplace_back(pi, ci);
        next.push_back(ci);
      }
    }
    if (next.empty()) break;
    frontier = next;
  }
  double minx = 0;
  for (const auto& n : out.nodes) minx = std::min(minx, n.x);
  if (minx < 0)
    for (auto& n : out.nodes) n.x -= minx - kMargin;
  return out;
}

std::string palette() {
  return "#2f6f9f,#a0522d,#3b7a45,#8a4f9e,#b8860b,#4a6fa5,#a14444,#2e7d7d";
}

// 按世代区间裁出独立一页：节点重新编号，边只保留两端都在页内的，
// y 坐标归零到本页顶部（跨页不再依赖 CSS page-break，页码由 CLI 写入文件名）。
layout slice_layout(const layout& full, int gen_lo, int gen_hi) {
  layout page;
  std::unordered_map<std::size_t, std::size_t> remap;
  double min_y = 0;
  for (std::size_t i = 0; i < full.nodes.size(); ++i) {
    const auto& n = full.nodes[i];
    if (n.gen < gen_lo || n.gen > gen_hi) continue;
    remap[i] = page.nodes.size();
    min_y = page.nodes.empty() ? n.y : std::min(min_y, n.y);
    page.nodes.push_back(n);
  }
  for (const auto& e : full.edges) {
    const auto a = remap.find(e.first), b = remap.find(e.second);
    if (a == remap.end() || b == remap.end()) continue;
    page.edges.emplace_back(a->second, b->second);
  }
  // 焦点若不在本页，落到第一个节点（保证高亮可见）
  page.focus_index = 0;
  if (const auto it = remap.find(full.focus_index); it != remap.end())
    page.focus_index = it->second;
  for (auto& n : page.nodes) n.y -= min_y;
  return page;
}

std::string render_svg(const layout& l, const ChartOptions& opt,
                       const std::vector<std::string>& warnings,
                       std::size_t page_count, int style,
                       std::size_t page_no = 1,
                       std::pair<int, int> gen_span = {1, 1},
                       bool overlaps_previous = false) {
  const double maxx = l.nodes.empty() ? kNodeW :
      std::max_element(l.nodes.begin(), l.nodes.end(),
                       [](const laid_out& a, const laid_out& b) { return a.x < b.x; })->x + kNodeW;
  const double maxy = l.nodes.empty() ? kNodeH :
      std::max_element(l.nodes.begin(), l.nodes.end(),
                       [](const laid_out& a, const laid_out& b) { return a.y < b.y; })->y + kNodeH;

  std::unordered_map<std::string, int> colors;
  // UTF-8 逐字节哈希作分色键（无 locale 依赖）
  for (const auto& n : l.nodes) {
    std::size_t h = 0;
    for (unsigned char c : n.branch) h = h * 31 + c;
    colors[n.branch] = static_cast<int>(h % 8);
  }

  std::ostringstream s;
  s << "<?xml version='1.0' encoding='UTF-8'?>\n"
    << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 "
    << static_cast<int>(maxx + kMargin * 2) << " "
    << static_cast<int>(maxy + kMargin * 2) << "' "
    << "width='100%' role='img'>\n"
    << "<title>" << esc(opt.title.empty() ? "世系图" : opt.title) << "</title>\n"
    << "<style>\n"
    << ".node rect{fill:#fff;stroke-width:1.2;}\n"
    << ".node text{font-family:'Source Han Sans SC','Noto Sans CJK SC',sans-serif;"
    << "font-size:12px;fill:#111;}\n"
    << ".node.focus rect{stroke-width:2.6;}\n"
    << ".node.collapsed{opacity:.45;}\n"
    << ".edge{stroke:#777;stroke-width:1;fill:none;}\n"
    << ".edge.spouse{stroke:#a0522d;stroke-dasharray:4 3;}\n"
    << ".warn{fill:#a32d2d;font-size:11px;}\n"
    << ".hint{fill:#555;font-size:11px;}\n"
    << "</style>\n"
    << "<text class='hint' x='" << kMargin << "' y='20'>"
    << esc(opt.title.empty() ? "世系图" : opt.title)
    << " · 节点 " << l.nodes.size() << " · 第 " << page_no
    << (page_count > 0 ? "/" + std::to_string(page_count) : "")
    << " 页 · 第 " << (gen_span.first + 1) << "–" << (gen_span.second + 1) << " 世"
    << (overlaps_previous ? "（苏式：与上页重叠首世）" : "")
    << " · 点击节点聚焦，输入姓名定位</text>\n";

  // 房支分色：每个节点用其 branch 色
  const std::vector<std::string> cs = [] {
    std::vector<std::string> v;
    std::string p = palette();
    std::size_t i = 0;
    while (i < p.size()) {
      const auto j = p.find(',', i);
      v.push_back(p.substr(i, j == std::string::npos ? std::string::npos : j - i));
      if (j == std::string::npos) break;
      i = j + 1;
    }
    return v;
  }();

  for (const auto& e : l.edges) {
    if (e.first >= l.nodes.size() || e.second >= l.nodes.size()) continue;
    const auto& a = l.nodes[e.first];
    const auto& b = l.nodes[e.second];
    const double x1 = a.x + kNodeW / 2 + kMargin;
    const double y1 = a.y + kNodeH + kMargin;
    const double x2 = b.x + kNodeW / 2 + kMargin;
    const double y2 = b.y + kMargin;
    // 父在上子在下；祖先图方向相反（gen 越大越上），但连线同样连接两端
    const double sy1 = a.gen < b.gen ? y1 : a.y + kMargin;
    const double sy2 = a.gen < b.gen ? y2 : b.y + kNodeH + kMargin;
    s << "<path class='edge' d='M" << static_cast<int>(x1) << " "
      << static_cast<int>(sy1) << " L" << static_cast<int>(x2) << " "
      << static_cast<int>(sy2) << "'/>\n";
  }

  // 环检测：edges 里同一节点既是某人的父又被其子指回，用 visited 标志简化标注
  std::unordered_set<std::string> seen;
  for (std::size_t i = 0; i < l.nodes.size(); ++i) {
    const auto& n = l.nodes[i];
    const bool dup = !seen.insert(n.person->id).second;
    const bool focus = i == l.focus_index;
    s << "<g class='node" << (focus ? " focus" : "") << (dup ? " collapsed" : "") << "' "
      << "data-name='" << esc(label_of(*n.person)) << "' "
      << "transform='translate(" << static_cast<int>(n.x + kMargin) << ","
      << static_cast<int>(n.y + kMargin) << ")'>"
      << "<rect width='" << kNodeW << "' height='" << kNodeH << "' rx='4' "
      << "stroke='" << cs[colors[n.branch] % cs.size()] << "'/>"
      << "<text x='6' y='14'>" << esc(label_of(*n.person)) << "</text>"
      << "<text x='6' y='28' class='hint'>" << esc(life_of(*n.person)) << "</text>"
      << "</g>\n";
  }
  std::size_t wy = 0;
  for (const auto& w : warnings) {
    s << "<text class='warn' x='" << kMargin << "' y='"
      << static_cast<int>(maxy + kMargin * 2 + 14 + wy * 14) << "'>"
      << esc(w) << "</text>\n";
    if (++wy >= 12) break;
  }
  s << "<text class='hint' x='" << kMargin << "' y='"
    << static_cast<int>(maxy + kMargin * 2 + 14 + (wy + 1) * 14)
    << "'>搜索: <input id='q' type='text' placeholder='输入姓名'/></text>\n"
    << "<script><![CDATA[\n"
    << "document.getElementById('q').addEventListener('input',function(e){\n"
    << " var v=e.target.value;document.querySelectorAll('g.node').forEach(function(n){\n"
    << "  n.style.display=(v&&n.getAttribute('data-name').indexOf(v)<0)?'none':'';\n"
    << " });});\n"
    << "document.querySelectorAll('g.node').forEach(function(n){\n"
    << " n.addEventListener('click',function(){\n"
    << "  document.querySelectorAll('g.node').forEach(function(x){x.classList.remove('focus');});\n"
    << "  n.classList.add('focus');});});\n"
    << "]]></script>\n</svg>\n";
  return s.str();
}

}  // namespace

const char* to_string(ChartStyle s) {
  switch (s) {
    case ChartStyle::Pedigree: return "pedigree";
    case ChartStyle::Descendant: return "descendant";
    case ChartStyle::Hourglass: return "hourglass";
    case ChartStyle::Wufu: return "wufu";
    case ChartStyle::European: return "european";
    case ChartStyle::Soviet: return "soviet";
  }
  return "descendant";
}

std::optional<ChartStyle> chart_style_from_string(const std::string& s) {
  if (s == "pedigree") return ChartStyle::Pedigree;
  if (s == "descendant") return ChartStyle::Descendant;
  if (s == "hourglass") return ChartStyle::Hourglass;
  if (s == "wufu") return ChartStyle::Wufu;
  if (s == "european") return ChartStyle::European;
  if (s == "soviet") return ChartStyle::Soviet;
  return std::nullopt;
}

Result<ChartResult> render_chart_svg(const ChartOptions& opt) {
  if (!opt.graph) {
    return Error{ErrorCode::InvalidArgument, "世系图缺少人物图"};
  }
  const auto& g = *opt.graph;
  std::vector<std::string> warnings;
  layout l;
  std::size_t pages = 1;

  switch (opt.style_hint) {
    case ChartStyle::Pedigree:
      l = pedigree(g, opt.focus_id, opt.depth, &warnings);
      break;
    case ChartStyle::Descendant:
      l = descendants(g, opt.focus_id, opt.depth, opt.show_spouses, &warnings);
      break;
    case ChartStyle::Hourglass: {
      // 上半祖先 + 下半后代：合并两张布局并错开 y
      auto up = pedigree(g, opt.focus_id, std::min(opt.depth, 3), &warnings);
      auto down = descendants(g, opt.focus_id, std::min(opt.depth, 3),
                              opt.show_spouses, &warnings);
      for (auto& n : up.nodes) n.y = kMargin + n.gen * kGapY;
      const int shift = static_cast<int>(up.nodes.size() ? 4 : 1);
      for (auto& n : down.nodes) n.y += kMargin + shift * kGapY;
      l = up;
      const std::size_t base = l.nodes.size();
      for (const auto& n : down.nodes) l.nodes.push_back(n);
      for (const auto& e : down.edges)
        l.edges.emplace_back(e.first + base, e.second + base);
      break;
    }
    case ChartStyle::Wufu: {
      // 九族五服严格矩阵：纵轴九族 × 横轴五服，服等由外置规则表逐对计算。
      auto matrix = render_wufu_matrix(opt);
      return matrix;  // 矩阵自带独立布局，不走下面的通用 render_svg
    }
    case ChartStyle::European:
    case ChartStyle::Soviet: {
      // 物理分页：每页独立完整 SVG，可单独打印/另存 PDF。
      // 欧式不重叠；苏式要求下一页首世与上一页末世相同（第五世两图共见）。
      auto full = descendants(g, opt.focus_id, opt.depth, false, &warnings);
      if (full.nodes.empty()) { l = full; break; }
      const int maxgen = [&] { int m = 0; for (const auto& n : full.nodes) m = std::max(m, n.gen); return m; }();
      const bool overlap = opt.style_hint == ChartStyle::Soviet;
      const int step = std::max(1, opt.page_size - (overlap ? 1 : 0));
      const int first_gen = [&] { int m = 1000000; for (const auto& n : full.nodes) m = std::min(m, n.gen); return m; }();
      ChartResult paged;
      paged.warnings = warnings;
      // 预计算总页数，供页眉显示"第 N/M 页"
      const std::size_t total_pages = [&]() {
        std::size_t n = 0;
        for (int g = first_gen; g <= maxgen; g += step) ++n;
        return n;
      }();
      for (int lo = first_gen; lo <= maxgen; lo += step) {
        const int hi = std::min(lo + opt.page_size - 1, maxgen);
        auto page_layout = slice_layout(full, lo, hi);
        ChartPage p;
        p.first_generation = lo;
        p.last_generation = hi;
        p.overlaps_previous = overlap && lo > first_gen;
        p.svg = render_svg(page_layout, opt, warnings, total_pages,
                           static_cast<int>(opt.style_hint),
                           paged.pages.size() + 1, {lo, hi},
                           p.overlaps_previous);
        paged.pages.push_back(std::move(p));
        if (hi == maxgen) break;
      }
      paged.node_count = full.nodes.size();
      paged.page_count = paged.pages.size();
      paged.svg = paged.pages.empty() ? std::string() : paged.pages[0].svg;
      return paged;
    }
  }

  ChartResult out;
  out.node_count = l.nodes.size();
  out.page_count = 1;
  out.warnings = warnings;
  // 非分页也按实际世代区间显示，避免固定 {1,1} 显示成"第2–2世"
  int min_gen = 0, max_gen = 0;
  if (!l.nodes.empty()) {
    min_gen = max_gen = l.nodes.front().gen;
    for (const auto& n : l.nodes) {
      min_gen = std::min(min_gen, n.gen);
      max_gen = std::max(max_gen, n.gen);
    }
  }
  out.svg = render_svg(l, opt, warnings, 1, static_cast<int>(opt.style_hint),
                       1, {min_gen, max_gen});
  out.pages.push_back({out.svg, min_gen, max_gen, false});
  return out;
}

}  // namespace genealogy::io
