// P0 出版管线测试：HTML 生成（纯函数，无需浏览器）。
//
// 验证点：三种版式、纸张/方向、字体嵌入与否、竖排 CSS、页码、
// HTML 转义（防注入）、空指针安全。

#include <catch2/catch_test_macros.hpp>

#include <genealogy/io/book_html.hpp>
#include <genealogy/model/book.hpp>
#include <genealogy/model/person.hpp>

#include <string>
#include <vector>

using namespace genealogy;

namespace {

Book sample_book() {
  Book b;
  b.name = "陈氏族谱";
  b.surname = "陈";
  b.hall_name = "德星堂";
  b.generation_poem = "德宗景文正";
  return b;
}

std::vector<Person> sample_persons() {
  Person p;
  p.id = "P1";
  p.family_name = "陈";
  p.given_name = "文远";
  p.generation_no = 1;
  p.birth_date = GenealogyDate::from_iso("1864-05-09").value();
  Person c;
  c.id = "P2";
  c.family_name = "陈";
  c.given_name = "承嗣";
  c.generation_no = 2;
  c.father_id = p.id;
  return {p, c};
}

}  // namespace

TEST_CASE("出版 HTML：三种版式与纸张方向") {
  const auto book = sample_book();
  const auto persons = sample_persons();
  for (auto tmpl : {io::BookTemplate::Register, io::BookTemplate::Genealogy,
                    io::BookTemplate::Biography}) {
    io::BookHtmlOptions opt;
    opt.book = &book;
    opt.persons = &persons;
    opt.tmpl = tmpl;
    opt.embed_fonts = false;  // 测试不读磁盘字体
    auto html = io::render_book_html(opt);
    REQUIRE(html.has_value());
    CHECK(html->find("<!DOCTYPE html>") != std::string::npos);
    CHECK(html->find("writing-mode:vertical-rl") != std::string::npos);
    CHECK(html->find("@page{size:210mm 297mm") != std::string::npos);
    CHECK(html->find("@bottom-center{content:counter(page);}") != std::string::npos);
    CHECK(html->find("陈氏族谱") != std::string::npos);
    CHECK(html->find("德宗景文正") != std::string::npos);
    CHECK(html->find("文远") != std::string::npos);
  }

  io::BookHtmlOptions opt;
  opt.book = &book;
  opt.persons = &persons;
  opt.paper = io::PaperSize::K16;
  opt.landscape = true;
  opt.embed_fonts = false;
  auto html = io::render_book_html(opt);
  REQUIRE(html.has_value());
  CHECK(html->find("@page{size:185mm 260mm;landscape") != std::string::npos);
}

TEST_CASE("出版 HTML：字体嵌入受控且缺字体时报错") {
  const auto book = sample_book();
  const auto persons = sample_persons();
  io::BookHtmlOptions opt;
  opt.book = &book;
  opt.persons = &persons;
  opt.embed_fonts = false;
  auto plain = io::render_book_html(opt);
  REQUIRE(plain.has_value());
  CHECK(plain->find("data:font/otf;base64") == std::string::npos);

  opt.embed_fonts = true;
  opt.font_dir = "D:/nonexistent-font-dir-for-test";
  auto embedded = io::render_book_html(opt);
  CHECK_FALSE(embedded.has_value());
  CHECK(embedded.error().code() == ErrorCode::IoError);
}

TEST_CASE("出版 HTML：空数据不崩、HTML 转义生效") {
  io::BookHtmlOptions opt;
  opt.embed_fonts = false;  // persons / book 全空
  auto html = io::render_book_html(opt);
  REQUIRE(html.has_value());
  CHECK(html->find("家谱") != std::string::npos);  // 标题回退

  Book evil = sample_book();
  evil.name = "<script>alert(1)</script>";
  Person p;
  p.id = "X";
  p.family_name = "<b>";
  p.given_name = "甲";
  std::vector<Person> ps{p};
  opt.book = &evil;
  opt.persons = &ps;
  auto escaped = io::render_book_html(opt);
  REQUIRE(escaped.has_value());
  CHECK(escaped->find("<script>") == std::string::npos);
  CHECK(escaped->find("&lt;script&gt;") != std::string::npos);
}
