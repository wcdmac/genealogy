// 自有标记语法解析测试。
#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/markup.hpp>

#include <string>

using namespace genealogy;

TEST_CASE("标记语法：卷章节与强制分页") {
  auto r = io::parse_markup("〔卷〕第一卷\n〔章〕第一章\n〔节〕第一节\n〔页〕末段");
  REQUIRE(r.has_value());
  bool vol = false, cha = false, sec = false, pb = false, text = false;
  for (const auto& n : r->nodes) {
    if (n.kind == io::MarkupKind::Volume) vol = true;
    if (n.kind == io::MarkupKind::Chapter) cha = true;
    if (n.kind == io::MarkupKind::Section) sec = true;
    if (n.kind == io::MarkupKind::PageBreak) pb = true;
    if (n.kind == io::MarkupKind::Text && n.text.find("末段") != std::string::npos)
      text = true;
  }
  CHECK(vol);
  CHECK(cha);
  CHECK(sec);
  CHECK(pb);
  CHECK(text);
}

TEST_CASE("标记语法：配对标记夹注眉批书耳专名线") {
  auto r = io::parse_markup(
      "正文〔注〕小字注释〔/注〕后文〔标〕孔子〔/标〕〔耳〕书耳〔/耳〕");
  REQUIRE(r.has_value());
  bool note = false, mark = false, ear = false;
  for (const auto& n : r->nodes) {
    if (n.kind == io::MarkupKind::Note) {
      note = true;
      CHECK(n.text == "小字注释");
    }
    if (n.kind == io::MarkupKind::Emphasis) {
      mark = true;
      CHECK(n.text == "孔子");
    }
    if (n.kind == io::MarkupKind::Ear) {
      ear = true;
      CHECK(n.text == "书耳");
    }
  }
  CHECK(note);
  CHECK(mark);
  CHECK(ear);
}

TEST_CASE("MARKUP_MISC_TEST") {
  auto r = io::parse_markup("句〔圈〕读〔点〕〔indent:3〕缩进〔未知〕保留");
  REQUIRE(r.has_value());
  bool circle = false, dot = false, indent = false, preserved = false;
  for (const auto& n : r->nodes) {
    if (n.kind == io::MarkupKind::Circle) circle = true;
    if (n.kind == io::MarkupKind::Dot) dot = true;
    if (n.kind == io::MarkupKind::Indent) {
      indent = true;
      CHECK(n.value == 3);
    }
    if (n.text.find("〔未知〕") != std::string::npos) preserved = true;
  }
  CHECK(circle);
  CHECK(dot);
  CHECK(indent);
  CHECK(preserved);
}

TEST_CASE("标记语法：未闭合告警且不吞内容") {
  auto r = io::parse_markup("正文〔注〕未闭合的注释");
  REQUIRE(r.has_value());
  REQUIRE(r->warnings.size() == 1);
  CHECK(r->warnings[0].find("未闭合") != std::string::npos);
  bool found = false;
  for (const auto& n : r->nodes)
    if (n.kind == io::MarkupKind::Note && n.text == "未闭合的注释") found = true;
  CHECK(found);
}

TEST_CASE("标记语法：indent 非法值告警") {
  auto r = io::parse_markup("〔indent:abc〕文本");
  REQUIRE(r.has_value());
  CHECK_FALSE(r->warnings.empty());
}
