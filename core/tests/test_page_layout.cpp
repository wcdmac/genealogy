// 页布局与夹注跨页续排测试（真实几何模型）。
#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/page_layout.hpp>
#include <genealogy/util/utf8.hpp>

#include <string>
#include <vector>

namespace {

genealogy::io::MarkupNode node(genealogy::io::MarkupKind k,
                               const std::string& text) {
  genealogy::io::MarkupNode n;
  n.kind = k;
  n.text = text;
  return n;
}

// 小版框：2 列 × 每列 3 行（6 半行）→ 每页正文 6 格
genealogy::io::LayoutGeometry tiny_geometry() {
  genealogy::io::LayoutGeometry g;
  g.frame_t = 0;
  g.frame_b = 3 * g.body_step + g.top_pad + g.bottom_pad;
  g.frame_r = g.center_x + g.center_w + 20 + 24 + g.col_gap;  // right-left=22 → 2 列
  return g;
}

}  // namespace

TEST_CASE("PAGE_LAYOUT short text") {
  genealogy::io::PageLayoutOptions opt;
  opt.source.push_back(node(genealogy::io::MarkupKind::Text, "短文本"));
  auto r = genealogy::io::paginate_content(opt);
  REQUIRE(r.has_value());
  CHECK(r->pages.size() == 1);
  CHECK(r->total_chars == 3);
  CHECK(r->continued_count == 0);
}

TEST_CASE("PAGE_LAYOUT geometry exact — body fills columns then page") {
  genealogy::io::PageLayoutOptions opt;
  opt.geometry = tiny_geometry();
  // 每页 2 列 × 3 行 = 6 正文格；给 8 字 → 恰好 2 页（第 2 页 2 字）
  std::string text;
  for (int i = 0; i < 8; ++i) text += "字";
  opt.source.push_back(node(genealogy::io::MarkupKind::Text, text));
  auto r = genealogy::io::paginate_content(opt);
  REQUIRE(r.has_value());
  CHECK(r->pages.size() == 2);
  std::size_t p0 = 0, p1 = 0;
  for (const auto& n : r->pages[0].nodes)
    if (n.kind == genealogy::io::MarkupKind::Text) p0 += genealogy::utf8_chars(n.text).size();
  for (const auto& n : r->pages[1].nodes)
    if (n.kind == genealogy::io::MarkupKind::Text) p1 += genealogy::utf8_chars(n.text).size();
  CHECK(p0 + p1 == 8);  // 正文零丢失
  CHECK(p0 == 6);  // 真实几何：第一页装满 6 格才换页
  CHECK(p1 == 2);
}

TEST_CASE("PAGE_LAYOUT note continuation at real row boundary") {
  genealogy::io::PageLayoutOptions opt;
  opt.geometry = tiny_geometry();
  std::string note;
  for (int i = 0; i < 20; ++i) note += "注";
  opt.source.push_back(node(genealogy::io::MarkupKind::Text, "正文"));
  opt.source.push_back(node(genealogy::io::MarkupKind::Note, note));
  auto r = genealogy::io::paginate_content(opt);
  REQUIRE(r.has_value());
  CHECK((r->pages.size() >= 2));
  CHECK((r->continued_count >= 1));
  CHECK(r->pages[1].continuation);
  bool has_cont = false;
  for (const auto& n : r->pages[1].nodes)
    if (n.kind == genealogy::io::MarkupKind::Continuation &&
        n.text.find("注续") != std::string::npos)
      has_cont = true;
  CHECK(has_cont);
  std::size_t note_chars = 0;
  for (const auto& page : r->pages)
    for (const auto& n : page.nodes)
      if (n.kind == genealogy::io::MarkupKind::Note)
        note_chars += genealogy::utf8_chars(n.text).size();  // 码点数，非字节数
  CHECK(note_chars == 20);  // 零丢失
}

TEST_CASE("PAGE_LAYOUT forced break overrides geometry") {
  genealogy::io::PageLayoutOptions opt;
  opt.use_geometry = false;
  opt.source.push_back(node(genealogy::io::MarkupKind::Text, "第一页"));
  opt.source.push_back(node(genealogy::io::MarkupKind::PageBreak, ""));
  opt.source.push_back(node(genealogy::io::MarkupKind::Text, "第二页"));
  auto r = genealogy::io::paginate_content(opt);
  REQUIRE(r.has_value());
  CHECK(r->pages.size() == 2);
  CHECK(r->continued_count == 0);
}
