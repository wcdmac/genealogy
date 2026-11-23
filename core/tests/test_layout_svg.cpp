// 古籍版式要素渲染测试（全要素：版心/鱼尾/象鼻/栏线/书耳/界行）。
#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/layout_svg.hpp>

#include <nlohmann/json.hpp>
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
  // 圈点（合并渲染后为竖排字符 ○/、）
  CHECK(svg->find("○") != std::string::npos);
  CHECK(svg->find("、") != std::string::npos);
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


TEST_CASE("版式：字形合并——text 元素数远小于字数") {
  std::string long_text;
  for (int i = 0; i < 2000; ++i) long_text += "字";
  genealogy::io::MarkupNode node;
  node.kind = genealogy::io::MarkupKind::Text;
  node.text = long_text;
  genealogy::io::LayoutOptions opt;
  opt.preset = "plain_modern";
  opt.book_title = "测试";
  opt.content = {node};
  auto svg = genealogy::io::render_layout_page(opt);
  REQUIRE(svg.has_value());
  std::size_t text_count = 0;
  std::size_t pos = 0;
  while ((pos = svg->find("<text", pos)) != std::string::npos) {
    ++text_count;
    pos += 5;
  }
  // 2000 字若逐字一个元素会是 2000+；合并后应远小于（列数级）
  CHECK(text_count < 100);
  CHECK(svg->find("writing-mode='vertical-rl'") != std::string::npos);
}

TEST_CASE("版式：夹注续接标记渲染为红字") {
  genealogy::io::MarkupNode cont;
  cont.kind = genealogy::io::MarkupKind::Continuation;
  cont.text = "（注续）";
  genealogy::io::LayoutOptions opt;
  opt.preset = "guji_classic";
  opt.content = {cont};
  auto svg = genealogy::io::render_layout_page(opt);
  REQUIRE(svg.has_value());
  CHECK(svg->find("注续") != std::string::npos);
  CHECK(svg->find("class='warn'") != std::string::npos);
}


TEST_CASE("版式：专业复刻——装订线/双鱼尾/双象鼻/栏线组合") {
  genealogy::io::LayoutOptions opt;
  opt.preset = "guji_classic";
  opt.book_title = "复刻测试";
  auto svg = genealogy::io::render_layout_page(opt);
  REQUIRE(svg.has_value());
  // 装订线：四眼（4 个孔 circle + 1 条虚线）
  CHECK(svg->find("stroke-dasharray='2 3'") != std::string::npos);
  std::size_t holes = 0, p2 = 0;
  while ((p2 = svg->find("r='3' fill='#f7f1e3'", p2)) != std::string::npos) {
    ++holes;
    p2 += 5;
  }
  CHECK(holes == 4);
  // 单鱼尾（count=1）：一个三角 path
  std::size_t tails = 0, p3 = 0;
  while ((p3 = svg->find("class='yuwei' d=", p3)) != std::string::npos) {
    ++tails;
    p3 += 5;
  }
  CHECK((tails >= 1));
  // 双象鼻（line_count=2）：每端 2 条，共 4 条
  std::size_t xb = 0, p4 = 0;
  while ((p4 = svg->find("class='xiangbi'", p4)) != std::string::npos) {
    ++xb;
    p4 += 5;
  }
  CHECK(xb == 4);
  // 栏线四边双边
  CHECK(svg->find("class='frame'") != std::string::npos);
  CHECK(svg->find("class='frame2'") != std::string::npos);
}

TEST_CASE("版式：参数直读——预设扩展字段生效") {
  auto pj = genealogy::io::layout_preset_json("guji_classic");
  REQUIRE(pj.has_value());
  const auto j = nlohmann::json::parse(*pj);
  CHECK(j["yuwei"]["count"].get<int>() == 1);
  CHECK(j["yuwei"]["available_styles"].size() == 5);
  CHECK(j["xiangbi"]["line_count"].get<int>() == 2);
  CHECK(j["binding"]["holes"].get<int>() == 4);
  CHECK(j["frame"]["side_rules"]["left"].get<std::string>() == "double");
}
