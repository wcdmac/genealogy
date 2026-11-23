// 字辈派语面板状态机（无 ImGui 依赖）。

#include <genealogy/gui/panels/poem_panel.hpp>

#include <genealogy/model/generation_poem.hpp>

namespace genealogy::gui {

namespace {
genealogy::PoemOwner owner(bool branch_scope) {
  return branch_scope ? genealogy::PoemOwner::Branch : genealogy::PoemOwner::Lineage;
}
}  // namespace

bool PoemPanel::reload(DbSession& session, std::string* why) {
  auto got = session.poems().get_poem(owner(branch_scope_), owner_id_buf_);
  if (!got) { *why = got.error().message(); return false; }
  entries_ = *got;
  // 把已有条目拼回联句原文（各条字符顺序即辈序）
  text_buf_.clear();
  for (const auto& e : entries_) text_buf_ += e.character;
  return true;
}

bool PoemPanel::set(DbSession& session, std::string* why) {
  if (text_buf_.empty()) { *why = "派语不能为空"; return false; }
  auto saved = session.poems().set_poem(owner(branch_scope_), owner_id_buf_, text_buf_);
  if (!saved) { *why = saved.error().message(); return false; }
  entries_ = *saved;
  conflicts_.clear();
  return true;
}

bool PoemPanel::check(DbSession& session, std::string* why) {
  auto found = session.poems().check_consistency(
      owner(branch_scope_), owner_id_buf_, 1);
  if (!found) { *why = found.error().message(); return false; }
  conflicts_.clear();
  for (const auto& c : *found) conflicts_.push_back(c.person_name + "：" + c.detail);
  return true;
}

void PoemPanel::clear() {
  text_buf_.clear();
  entries_.clear();
  conflicts_.clear();
}

}  // namespace genealogy::gui
