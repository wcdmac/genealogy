// 页布局与夹注跨页续排测试。
#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/page_layout.hpp>

#include <string>
#include <vector>

TEST_CASE("PAGE_LAYOUT short text") {
  genealogy::io::PageLayoutOptions opt;
  opt.capacity = 100;
  genealogy::io::MarkupNode node;
  node.kind = genealogy::io::MarkupKind::Text;
  node.text = "短文本";
  opt.source.push_back(node);
  auto r = genealogy::io::paginate_content(opt);
  REQUIRE(r.has_value());
  CHECK(r->pages.size() == 1);
  CHECK(r->pages[0].nodes.size() == 1);
  CHECK(r->total_chars == 3);
  CHECK(r->continued_count == 0);
}

TEST_CASE("PAGE_LAYOUT note continuation") {
  std::string long_note;
  for (int i = 0; i < 30; ++i) long_note += "注" + std::to_string(i);
  genealogy::io::PageLayoutOptions opt;
  opt.capacity = 10;
  opt.note_chars_per_cell = 2;
  genealogy::io::MarkupNode text1, note, text2;
  text1.kind = genealogy::io::MarkupKind::Text;
  text1.text = "正文";
  note.kind = genealogy::io::MarkupKind::Note;
  note.text = long_note;
  text2.kind = genealogy::io::MarkupKind::Text;
  text2.text = "后文";
  opt.source.push_back(text1);
  opt.source.push_back(note);
  opt.source.push_back(text2);
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
  for (const auto& p : r->pages)
    for (const auto& n : p.nodes)
      if (n.kind == genealogy::io::MarkupKind::Note) note_chars += n.text.size();
  CHECK(note_chars == long_note.size());
}

TEST_CASE("PAGE_LAYOUT forced break") {
  genealogy::io::PageLayoutOptions opt;
  opt.capacity = 0;
  genealogy::io::MarkupNode n1, n2, n3;
  n1.kind = genealogy::io::MarkupKind::Text;
  n1.text = "第一页";
  n2.kind = genealogy::io::MarkupKind::PageBreak;
  n3.kind = genealogy::io::MarkupKind::Text;
  n3.text = "第二页";
  opt.source.push_back(n1);
  opt.source.push_back(n2);
  opt.source.push_back(n3);
  auto r = genealogy::io::paginate_content(opt);
  REQUIRE(r.has_value());
  CHECK(r->pages.size() == 2);
  CHECK(r->continued_count == 0);
}
