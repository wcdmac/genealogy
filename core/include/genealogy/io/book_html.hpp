#pragma once

// P0 出版管线：从领域对象生成自包含 HTML，交系统浏览器打印/另存 PDF。
//
// 为什么放 core：纯数据→文本变换，无 UI/网络/系统依赖，可被 CLI 与
// 未来的 GUI 共用（ADR-1）。字体以 base64 内嵌，产物单文件离线可用。
//
// 打印实现方式：HTML CSS @page + writing-mode:vertical-rl，
// 用户在浏览器 Ctrl+P 另存 PDF——绕开所有 LGPL/EPL PDF 引擎。

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/book.hpp>
#include <genealogy/model/person.hpp>

namespace genealogy::io {

enum class BookTemplate { Register, Genealogy, Biography };
enum class PaperSize { A4, K16 };

struct BookHtmlOptions {
  const Book* book = nullptr;
  const std::vector<Person>* persons = nullptr;
  BookTemplate tmpl = BookTemplate::Register;
  PaperSize paper = PaperSize::A4;
  bool landscape = false;
  // M8.3 脱敏开关（与 GEDCOM respect_privacy 同口径）：开启时在世非公开
  // 人物抹日期（private）或缩到年份（kin）。默认关——与 GEDCOM 现状一致，
  // 分发谱书前建议显式开启。
  bool respect_privacy = false;
  // 字体源目录（含 SourceHan*.otf 与 OFL-*.txt）；空则只引用系统字体
  std::string font_dir;
  // 是否把字体 base64 内嵌进 HTML（单文件离线，产物约 60MB）
  bool embed_fonts = true;
};

// 生成完整 HTML 文档。失败仅因字体读取错误（调用方可降级 embed_fonts=false）。
Result<std::string> render_book_html(const BookHtmlOptions& opt);

const char* to_string(BookTemplate t);
const char* to_string(PaperSize p);

}  // namespace genealogy::io
