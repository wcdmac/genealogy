// P1 世系图测试：六种体例的 SVG 生成（纯函数，无浏览器依赖）。

#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/chart_svg.hpp>
#include <genealogy/lineage/graph.hpp>

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
    // 可读性特性：焦点样式 + 搜索脚本 + 房支分色（rect stroke）
    CHECK(r->svg.find("node.focus") != std::string::npos);
    CHECK(r->svg.find("addEventListener('input'") != std::string::npos);
    CHECK(r->svg.find("stroke='#") != std::string::npos);
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
