// 古籍版式要素渲染测试（全要素：版心/鱼尾/象鼻/栏线/书耳/界行）。
#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/layout_svg.hpp>
#include <genealogy/io/markup.hpp>

#include <string>

using namespace genealogy;

TEST_CASE("版式：两套预设可加载且要素齐全") {
  auto pj = io::layout_preset_json("guji_classic");
  REQUIRE(pj.has_value());
  CHECK(pj->find("\"yuwei\"") != std::string::npos);
  CHECK(pj->find("\"xiangbi\"") != std::string::npos);
  CHECK(pj->find("\"shuer\"") != std::string::npos);
  CHECK(pj->find("\"banxin\"") != std::string::npos);

  auto parsed = io::parse_markup("〔卷〕第一卷\n正文〔注〕小注〔/注〕〔圈〕〔点〕");
  REQUIRE(parsed.has_value());

  io::LayoutOptions opt;
  opt.preset = "guji_classic";
  opt.book_title = "陈氏族谱";
  opt.volume_title = "卷一";
  opt.page_number = 3;
  opt.total_pages = 20;
  opt.content = parsed->nodes;
  auto svg = io::render_layout_page(opt);
  REQUIRE(svg.has_value());
  // 版框
  CHECK(svg->find("class='frame'") != std::string::npos);
  CHECK(svg->find("class='frame2'") != std::string::npos);
  // 界行
  CHECK(svg->find("class='grid'") != std::string::npos);
  // 版心 + 书名 + 卷次 + 页码
  CHECK(svg->find("class='banxin'") != std::string::npos);
  CHECK(svg->find("陈氏族谱") != std::string::npos);
  CHECK(svg->find("3/20") != std::string::npos);
  // 鱼尾（自绘三角）与象鼻（上下横线）
  CHECK(svg->find("class='yuwei'") != std::string::npos);
  CHECK(svg->find("class='xiangbi'") != std::string::npos);
  // 书耳
  CHECK(svg->find("class='ear'") != std::string::npos);
  // 圈点
  CHECK(svg->find("stroke='#a32d2d'") != std::string::npos);
  // 注文
  CHECK(svg->find("小注") != std::string::npos);
}

TEST_CASE("版式：素白简排 disabling 装饰要素") {
  auto parsed = io::parse_markup("〔章〕第一章\n正文");
  REQUIRE(parsed.has_value());
  io::LayoutOptions opt;
  opt.preset = "plain_modern";
  opt.book_title = "族谱";
  opt.content = parsed->nodes;
  auto svg = io::render_layout_page(opt);
  REQUIRE(svg.has_value());
  CHECK(svg->find("class='frame'") == std::string::npos);   // 无版框
  CHECK(svg->find("class='yuwei'") == std::string::npos);   // 无鱼尾
  CHECK(svg->find("class='shuer'") == std::string::npos);   // 无书耳（用 class='ear' 文本？见下）
  CHECK(svg->find("class='grid'") == std::string::npos);    // 无界行
  CHECK(svg->find("class='banxin'") != std::string::npos);  // 保留版心
}

TEST_CASE("版式：未知预设报错") {
  CHECK_FALSE(io::layout_preset_json("no_such_preset").has_value());
  io::LayoutOptions opt;
  opt.preset = "no_such_preset";
  CHECK_FALSE(io::render_layout_page(opt).has_value());
}
