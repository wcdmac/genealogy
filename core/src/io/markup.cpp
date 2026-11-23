#include <genealogy/io/markup.hpp>

#include <algorithm>
#include <cctype>
#include <map>

#include <genealogy/util/utf8.hpp>

namespace genealogy::io {

namespace {

struct TagInfo {
  MarkupKind kind;
  bool paired;
};

const std::map<std::string, TagInfo>& tags() {
  static const std::map<std::string, TagInfo> t = {
      {"卷", {MarkupKind::Volume, false}},
      {"章", {MarkupKind::Chapter, false}},
      {"节", {MarkupKind::Section, false}},
      {"注", {MarkupKind::Note, true}},
      {"批", {MarkupKind::Marginal, true}},
      {"耳", {MarkupKind::Ear, true}},
      {"圈", {MarkupKind::Circle, false}},
      {"点", {MarkupKind::Dot, false}},
      {"标", {MarkupKind::Emphasis, true}},
      {"页", {MarkupKind::PageBreak, false}},
  };
  return t;
}

bool is_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

}  // namespace

const char* to_string(MarkupKind k) {
  switch (k) {
    case MarkupKind::Text: return "text";
    case MarkupKind::Volume: return "volume";
    case MarkupKind::Chapter: return "chapter";
    case MarkupKind::Section: return "section";
    case MarkupKind::Note: return "note";
    case MarkupKind::Marginal: return "marginal";
    case MarkupKind::Ear: return "ear";
    case MarkupKind::Circle: return "circle";
    case MarkupKind::Dot: return "dot";
    case MarkupKind::Emphasis: return "emphasis";
    case MarkupKind::PageBreak: return "pagebreak";
    case MarkupKind::Indent: return "indent";
    case MarkupKind::Continuation: return "continuation";
  }
  return "text";
}

Result<MarkupResult> parse_markup(const std::string& source) {
  MarkupResult out;
  // 标记字节长度从共用 UTF-8 工具推导，不硬编码——
  // 〔 〕是三字节，手写 open+1 曾导致 indent→dent、中文标记全失配。
  const std::size_t open_len = utf8_chars("〔")[0].size();
  const std::size_t close_len = utf8_chars("〕")[0].size();
  std::size_t pos = 0;
  std::size_t line = 1;

  // 把一段纯文本作为 Text 节点（空则不产生）
  auto push_text = [&](const std::string& s) {
    if (!s.empty()) out.nodes.push_back({MarkupKind::Text, s, 0, line});
  };

  while (pos < source.size()) {
    const std::size_t open = source.find("〔", pos);
    if (open == std::string::npos) {
      push_text(source.substr(pos));
      break;
    }
    if (open > pos) push_text(source.substr(pos, open - pos));
    const std::size_t close = source.find("〕", open + 1);
    if (close == std::string::npos) {
      push_text(source.substr(open));
      break;
    }
    line += static_cast<std::size_t>(
        std::count(source.begin() + pos, source.begin() + close, '\n'));
    // 踩坑：〔 〕是 UTF-8 三字节（E3 80 94 / E3 80 95），
    // inner 起点必须是 open+3 而非 open+1——否则把首字节切掉，
    // indent:N 变成 "dent:N"，中文标记全部失配（与历法 find('年') 窄化同源）。
    const std::string inner = source.substr(open + open_len, close - open - open_len);
    pos = close + close_len;  // 跳过整个 〕 码位

    // 关闭标记：不再单独产生节点，内容已在开始时写入
    if (!inner.empty() && inner[0] == '/') continue;

    // indent:N
    if (inner.rfind("indent:", 0) == 0) {
      const std::string num = inner.substr(7);
      if (!is_digits(num)) {
        out.warnings.push_back("第 " + std::to_string(line) + " 行 indent 值非法: " + num);
        push_text("〔" + inner + "〕");
      } else {
        // 全数字但可能远超 int 范围（实测 stoi 抛 out_of_range）。
        // 手工累加并 clamp：只取有效区间，避免 Result 语义被异常穿透。
        constexpr long long kMaxIndent = 1000000;
        long long value = 0;
        for (char ch : num) {
          value = value * 10 + (ch - '0');
          if (value > kMaxIndent) { value = kMaxIndent; break; }
        }
        out.warnings.push_back("第 " + std::to_string(line) +
                               " 行 indent 值已限制到 " +
                               std::to_string(value));
        out.nodes.push_back({MarkupKind::Indent, "",
                             static_cast<int>(value), line});
      }
      continue;
    }

    // 查表
    MarkupKind kind = MarkupKind::Text;
    bool paired = false, matched = false;
    for (const auto& kv : tags()) {
      if (kv.first == inner) {
        kind = kv.second.kind;
        paired = kv.second.paired;
        matched = true;
        break;
      }
    }
    if (!matched) {
      push_text("〔" + inner + "〕");
      continue;
    }

    if (!paired) {
      // 非配对标记：直接一个节点，文本为空（卷/章的标题文本随后作为 Text）
      out.nodes.push_back({kind, "", 0, line});
      continue;
    }

    // 配对标记：从 pos 起找对应的 〔/name〕，中间文本即内容。
    const std::string closing = "〔/" + inner + "〕";
    const std::size_t end = source.find(closing, pos);
    if (end == std::string::npos) {
      // 未闭合：把余下全部作为内容，并告警
      const std::string content = source.substr(pos);
      out.nodes.push_back({kind, content, 0, line});
      out.warnings.push_back("第 " + std::to_string(line) + " 行标记〔" + inner + "〕未闭合");
      pos = source.size();
      break;
    }
    out.nodes.push_back({kind, source.substr(pos, end - pos), 0, line});
    line += static_cast<std::size_t>(
        std::count(source.begin() + pos, source.begin() + end, '\n'));
    pos = end + closing.size();
  }
  return out;
}

}  // namespace genealogy::io
