#pragma once

// 自有标记语法解析器（P2 排版增强）。
//
// 符号集为本项目自行设计（中文全角 〔〕，公版标点），不采用墨韵/易籍/
// 亦句/GujiStudio 的具体符号——只借鉴「标记与正文同处一地、改字即重排」
// 这一功能思想。所有实现为自研代码，不引用任何第三方源码。
//
// 语法：
//   〔卷〕文本        卷标题（新页）
//   〔章〕文本        章标题（新页）
//   〔节〕文本        节标题
//   〔注〕…〔/注〕    双行夹注（小字）
//   〔批〕…〔/批〕    眉批（版框上方）
//   〔耳〕…〔/耳〕    书耳（左上角耳框）
//   〔圈〕            句读圈（不占字格）
//   〔点〕            句读点（不占字格）
//   〔标〕…〔/标〕    专名线
//   〔页〕            强制分页
//   〔indent:N〕      本行缩进 N 字（N 非负整数）
// 未识别的 〔…〕 原样保留为文本，不吞内容。

#include <string>
#include <vector>

#include <genealogy/error.hpp>

namespace genealogy::io {

enum class MarkupKind {
  Text,       // 正文
  Volume,     // 〔卷〕
  Chapter,    // 〔章〕
  Section,    // 〔节〕
  Note,       // 〔注〕双行夹注
  Marginal,   // 〔批〕眉批
  Ear,        // 〔耳〕书耳
  Circle,     // 〔圈〕句读圈
  Dot,        // 〔点〕句读点
  Emphasis,   // 〔标〕专名线
  PageBreak,  // 〔页〕
  Indent,     // 〔indent:N〕（值在 value）
};

struct MarkupNode {
  MarkupKind kind = MarkupKind::Text;
  std::string text;
  int value = 0;          // Indent 的字数
  std::size_t line = 0;   // 源行号（1-based，供错误定位）
};

struct MarkupResult {
  std::vector<MarkupNode> nodes;
  std::vector<std::string> warnings;  // 未闭合标记等
};

Result<MarkupResult> parse_markup(const std::string& source);
const char* to_string(MarkupKind k);

}  // namespace genealogy::io
