#include <genealogy/io/page_layout.hpp>

#include <genealogy/util/utf8.hpp>

#include <algorithm>
#include <string>

namespace genealogy::io {

namespace {

// 一段文本的码点数（用共用工具，不手写 UTF-8 偏移）
std::size_t cp_count(const std::string& s) {
  return utf8_chars(s).size();
}

// 取前 n 个码点的子串，返回 {前缀, 余下}
std::pair<std::string, std::string> split_cps(const std::string& s, std::size_t n) {
  const auto cps = utf8_chars(s);
  if (n >= cps.size()) return {s, {}};
  std::string head, tail;
  for (std::size_t i = 0; i < cps.size(); ++i) {
    if (i < n) head.append(cps[i]);
    else tail.append(cps[i]);
  }
  return {head, tail};
}

}  // namespace

Result<PageLayout> paginate_content(const PageLayoutOptions& opt) {
  PageLayout out;
  LaidOutPage cur;
  std::size_t used = 0;

  auto flush = [&](bool next_continued) {
    out.pages.push_back(cur);
    cur = LaidOutPage{};
    cur.continuation = next_continued;
    used = 0;
  };

  for (const auto& node : opt.source) {
    if (node.kind == MarkupKind::PageBreak) {
      flush(false);
      continue;
    }
    // 夹注按 note_chars_per_cell 折抵格数：两字一格 → 字数/2
    const bool is_note = node.kind == MarkupKind::Note ||
                         node.kind == MarkupKind::Marginal ||
                         node.kind == MarkupKind::Ear;
    const int divisor = is_note ? std::max(1, opt.note_chars_per_cell) : 1;
    std::string remaining = node.text;
    bool first_chunk = true;

    while (true) {
      const std::size_t nodes_cps = cp_count(remaining);
      const std::size_t cost = (nodes_cps + divisor - 1) / divisor;
      const std::size_t room = opt.capacity == 0 ? nodes_cps
                                                 : (opt.capacity > used ? opt.capacity - used : 0);
      if (cost <= room || opt.capacity == 0) {
        MarkupNode piece = node;
        piece.text = remaining;
        cur.nodes.push_back(piece);
        used += cost;
        out.total_chars += nodes_cps;
        break;
      }

      // 装不下：按剩余格数切出本页能放的部分（夹注按 div 倍码点）
      const std::size_t take_cps = room * divisor;
      if (take_cps == 0) {
        // 当前页已满，换页再试（标记续接）
        flush(true);
        ++out.continued_count;
        continue;
      }
      auto [head, tail] = split_cps(remaining, take_cps);
      MarkupNode piece = node;
      piece.text = head;
      cur.nodes.push_back(piece);
      out.total_chars += cp_count(head);
      flush(true);
      ++out.continued_count;
      // 下一页插入续接标记，随后继续排余下夹注
      MarkupNode cont;
      cont.kind = MarkupKind::Continuation;
      cont.text = is_note ? "（注续）" : "（续）";
      cur.nodes.push_back(cont);
      used += cp_count(cont.text);
      remaining = tail;
    }
  }
  if (!cur.nodes.empty() || out.pages.empty()) out.pages.push_back(cur);
  return out;
}

}  // namespace genealogy::io
