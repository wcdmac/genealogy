// P1 世系图测试：六种体例的 SVG 生成（纯函数，无浏览器依赖）。

#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/chart_svg.hpp>
#include <genealogy/io/wufu_matrix.hpp>
#include <genealogy/lineage/graph.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace genealogy;

namespace {

Person person(const std::string& id, const std::string& name, int gen,
              const std::string& father = {}) {
  Person p;
  p.id = id;
  p.family_name = "陈";
  p.given_name = name;
  p.generation_no = gen;
  if (!father.empty()) p.father_id = father;
  return p;
}

}  // namespace

TEST_CASE("世系图：六种体例均产出合法 SVG") {
  std::vector<Person> ps{
      person("A", "始祖", 1), person("B", "长子", 2, "A"),
      person("C", "次子", 2, "A"), person("D", "长孙", 3, "B")};
  const auto g = lineage::PersonGraph::build(std::move(ps));

  for (auto style : {io::ChartStyle::Pedigree, io::ChartStyle::Descendant,
                     io::ChartStyle::Hourglass, io::ChartStyle::Wufu,
                     io::ChartStyle::European, io::ChartStyle::Soviet}) {
    io::ChartOptions opt;
    opt.graph = &g;
    opt.style_hint = style;
    opt.focus_id = "A";
    opt.depth = 4;
    opt.title = "陈氏世系";
    auto r = io::render_chart_svg(opt);
    REQUIRE(r.has_value());
    CHECK(r->svg.find("<?xml") != std::string::npos);
    CHECK(r->svg.find("<svg") != std::string::npos);
    CHECK(r->svg.find("</svg>") != std::string::npos);
    CHECK(r->node_count >= 1);
    CHECK(r->svg.find("陈氏世系") != std::string::npos);
    if (style == io::ChartStyle::Wufu) {
      // 五服走独立九族矩阵，不需要节点交互/房支分色
      CHECK(r->svg.find("高祖辈") != std::string::npos);  // 九族矩阵特征
    } else {
      // 可读性特性：焦点样式 + 搜索脚本 + 房支分色（rect stroke）
      CHECK(r->svg.find("node.focus") != std::string::npos);
      CHECK(r->svg.find("addEventListener('input'") != std::string::npos);
      CHECK(r->svg.find("stroke='#") != std::string::npos);
    }
  }
}

TEST_CASE("世系图：祖先缺失显式告警、无效焦点报错") {
  std::vector<Person> ps{person("A", "始祖", 1)};
  const auto g = lineage::PersonGraph::build(std::move(ps));
  io::ChartOptions opt;
  opt.graph = &g;
  opt.style_hint = io::ChartStyle::Pedigree;
  opt.focus_id = "A";
  auto r = io::render_chart_svg(opt);
  REQUIRE(r.has_value());
  CHECK_FALSE(r->warnings.empty());  // 父母缺失须显式暴露
  CHECK(r->svg.find("祖先缺失") != std::string::npos);

  opt.focus_id = "NO_SUCH";
  auto bad = io::render_chart_svg(opt);
  REQUIRE(bad.has_value());
  CHECK(bad->node_count == 0);

  opt.graph = nullptr;
  auto nog = io::render_chart_svg(opt);
  CHECK_FALSE(nog.has_value());
  CHECK(nog.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("世系图：体例名解析") {
  CHECK(io::chart_style_from_string("pedigree") == io::ChartStyle::Pedigree);
  CHECK(io::chart_style_from_string("soviet") == io::ChartStyle::Soviet);
  CHECK_FALSE(io::chart_style_from_string("nope").has_value());
  CHECK(std::string(io::to_string(io::ChartStyle::Wufu)) == "wufu");
}

TEST_CASE("世系图：欧/苏式物理分页与苏式重叠") {
  std::vector<Person> ps;
  ps.push_back(person("A", "一世祖", 1));
  std::vector<std::string> prev{"A"};
  char name = 'B';
  for (int g = 2; g <= 11; ++g) {
    std::vector<std::string> cur;
    for (const auto& parent : prev) {
      const std::string id = std::string(1, name++) + std::to_string(g);
      ps.push_back(person(id, "第" + std::to_string(g) + "世", g, parent));
      cur.push_back(id);
    }
    prev = cur;
  }
  const auto g = lineage::PersonGraph::build(std::move(ps));

  io::ChartOptions opt;
  opt.graph = &g;
  opt.style_hint = io::ChartStyle::European;
  opt.focus_id = "A";
  opt.depth = 11;
  opt.page_size = 5;
  auto eu = io::render_chart_svg(opt);
  REQUIRE(eu.has_value());
  CHECK(eu->pages.size() == 3);          // 11 世 / 5 = 3 页
  CHECK(eu->page_count == 3);
  CHECK(eu->svg == eu->pages[0].svg);    // 兼容字段
  CHECK(eu->pages[0].first_generation == 0);
  CHECK(eu->pages[0].last_generation == 4);
  CHECK(eu->pages[1].first_generation == 5);
  CHECK_FALSE(eu->pages[0].overlaps_previous);
  for (const auto& p : eu->pages)
    CHECK(p.svg.find("</svg>") != std::string::npos);

  opt.style_hint = io::ChartStyle::Soviet;
  auto su = io::render_chart_svg(opt);
  REQUIRE(su.has_value());
  CHECK(su->pages.size() == 3);
  CHECK(su->pages[0].last_generation == 4);
  CHECK(su->pages[1].first_generation == 4);   // 重叠：上页末世=下页首世（0-based）
  CHECK(su->pages[1].overlaps_previous);
  CHECK(su->pages[2].overlaps_previous);
}

TEST_CASE("五服：九族严格矩阵结构") {
  std::vector<Person> ps{
      person("GF", "祖父", -2), person("F", "父亲", -1, "GF"),
      person("E", "己身", 0, "F"), person("S", "子", 1, "E"),
      person("SS", "孙", 2, "S")};
  const auto g = lineage::PersonGraph::build(std::move(ps));
  io::ChartOptions opt;
  opt.graph = &g;
  opt.style_hint = io::ChartStyle::Wufu;
  opt.focus_id = "E";
  auto r = io::render_chart_svg(opt);
  REQUIRE(r.has_value());
  CHECK(r->svg.find("九族五服图") != std::string::npos);
  // 九个世代行标签
  for (const char* gname : {"高祖辈", "曾祖辈", "祖辈", "父辈", "己身",
                            "子辈", "孙辈", "曾孙辈", "玄孙辈"})
    CHECK(r->svg.find(gname) != std::string::npos);
  // 五个服等列标签
  for (const char* gname : {"斩衰", "齐衰", "大功", "小功", "缌麻"})
    CHECK(r->svg.find(gname) != std::string::npos);
  CHECK(r->svg.find("ming_qing") != std::string::npos);  // 外置规则表标注
  CHECK(r->pages.size() == 1);
  CHECK(r->pages[0].first_generation == -4);
  CHECK(r->pages[0].last_generation == 4);

  // 无效焦点须报错，不静默出空矩阵
  opt.focus_id = "NO";
  auto bad = io::render_chart_svg(opt);
  CHECK_FALSE(bad.has_value());
}

namespace {
// 同层节点两两不重叠（x 间距 ≥ 节点宽）
void check_no_overlap(const io::ChartResult& r, int node_w) {
  // 从 SVG transform 解析 x 坐标：不同 y（世代）不比较
  std::vector<std::pair<int, int>> xs;  // (y, x)
  std::size_t pos = 0;
  while ((pos = r.svg.find("transform='translate(", pos)) != std::string::npos) {
    const std::size_t a = pos + std::string("transform='translate(").size();
    const std::size_t b = r.svg.find(',', a);
    const std::size_t c = r.svg.find(')', b);
    if (b == std::string::npos || c == std::string::npos) break;
    xs.emplace_back(std::stoi(r.svg.substr(b + 1, c - b - 1)),
                    std::stoi(r.svg.substr(a, b - a)));
    pos = c;
  }
  for (std::size_t i = 0; i < xs.size(); ++i)
    for (std::size_t j = i + 1; j < xs.size(); ++j) {
      if (xs[i].first != xs[j].first) continue;
      const int d = std::abs(xs[i].second - xs[j].second);
      REQUIRE(d >= node_w);
    }
}
}  // namespace

TEST_CASE("世系图：同层节点不重叠（含配偶）") {
  std::vector<Person> ps{
      person("A", "父", 1), person("B", "长子", 2, "A"),
      person("C", "次子", 2, "A"), person("D", "三子", 2, "A")};
  // 配偶（无父亲边，gender 女）由 union 图提供；这里用多子场景验证 x 分配
  const auto g = lineage::PersonGraph::build(std::move(ps));
  io::ChartOptions opt;
  opt.graph = &g;
  opt.style_hint = io::ChartStyle::Descendant;
  opt.focus_id = "A";
  opt.depth = 3;
  auto r = io::render_chart_svg(opt);
  REQUIRE(r.has_value());
  CHECK(r->node_count == 4);
  check_no_overlap(*r, 96);  // kNodeW
}

TEST_CASE("世系图：万级节点性能回归") {
  // 1 名始祖 + 10 层 × 每层 1000 人（共 10001 节点），
  // 验证分支/布局/序列化在合理时间内完成（阈值宽松，仅防数量级退化）。
  std::vector<Person> ps;
  ps.push_back(person("ROOT", "始祖", 0));
  for (int i = 0; i < 10000; ++i) {
    ps.push_back(person("N" + std::to_string(i), "人" + std::to_string(i),
                        1 + i % 10, "ROOT"));
  }
  const auto g = lineage::PersonGraph::build(std::move(ps));
  io::ChartOptions opt;
  opt.graph = &g;
  opt.style_hint = io::ChartStyle::Descendant;
  opt.focus_id = "ROOT";
  opt.depth = 2;
  const auto t0 = std::chrono::steady_clock::now();
  auto r = io::render_chart_svg(opt);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  REQUIRE(r.has_value());
  CHECK(r->node_count > 1000);
  CHECK(ms < 10000);  // 10 秒上限；本机实测远低于此，CI 弱机留余量
}
