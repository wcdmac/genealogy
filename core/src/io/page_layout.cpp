#include <genealogy/io/page_layout.hpp>

#include <genealogy/util/utf8.hpp>

#include <utility>

namespace genealogy::io {

namespace {

bool is_note_kind(MarkupKind k) {
  return k == MarkupKind::Note || k == MarkupKind::Marginal || k == MarkupKind::Ear;
}

// 续接标记文本
const char* continuation_text(MarkupKind k) {
  return is_note_kind(k) ? "（注续）" : "（续）";
}

}  // namespace

Result<PageLayout> paginate_content(const PageLayoutOptions& opt) {
  PageLayout out;
  LaidOutPage cur;
  TextCursor cursor(opt.geometry);

  // 当前页正在累积的节点下标（vector 重分配后下标仍有效）
  std::size_t active = 0;
  bool has_active = false;

  auto finalize_active = [&]() { has_active = false; active = 0; };
  auto flush = [&](bool next_continued) {
    finalize_active();
    out.pages.push_back(cur);
    cur = LaidOutPage{};
    cur.continuation = next_continued;
    cursor.reset();
  };
  auto ensure_active = [&](const MarkupNode& src) {
    if (has_active && cur.nodes[active].kind == src.kind) return;
    MarkupNode n;
    n.kind = src.kind;
    n.line = src.line;
    cur.nodes.push_back(n);
    active = cur.nodes.size() - 1;
    has_active = true;
  };

  for (const auto& node : opt.source) {
    if (node.kind == MarkupKind::PageBreak) {
      finalize_active();
      flush(false);
      continue;
    }

    // 圈/点/缩进/续标记：占位小，直接落到当前页（不触发自动续页）
    if (node.kind == MarkupKind::Circle || node.kind == MarkupKind::Dot ||
        node.kind == MarkupKind::Indent || node.kind == MarkupKind::Continuation) {
      finalize_active();
      cur.nodes.push_back(node);
      continue;
    }

    const bool note = is_note_kind(node.kind);
    for (const auto& cp : utf8_chars(node.text)) {
      TextCursor::Placement place;
      bool ok = opt.use_geometry ? cursor.place(note, place) : true;
      if (!ok) {
        // 真实几何：本页最后一列已满 → 换页续排
        flush(true);
        ++out.continued_count;
        MarkupNode cont;
        cont.kind = MarkupKind::Continuation;
        cont.text = continuation_text(node.kind);
        cur.nodes.push_back(cont);
        finalize_active();
        cursor.place(note, place);  // 新页必然放得下
      }
      ensure_active(node);
      cur.nodes[active].text.append(cp);
      ++out.total_chars;
    }
  }
  if (!cur.nodes.empty() || out.pages.empty()) out.pages.push_back(cur);
  out.total_chars = 0;
  for (const auto& page : out.pages)
    for (const auto& n : page.nodes) out.total_chars += utf8_chars(n.text).size();
  return out;
}

}  // namespace genealogy::io
