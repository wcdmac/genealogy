// 支派面板状态机（无 ImGui 依赖，可无头测）。

#include <genealogy/gui/panels/branch_panel.hpp>

#include <genealogy/ulid.hpp>

namespace genealogy::gui {

void BranchPanel::sync_draft() {
  draft_.name = name_buf_;
  draft_.generation_char = generation_buf_.empty()
      ? std::nullopt : std::optional<std::string>(generation_buf_);
  draft_.note = note_buf_.empty() ? std::nullopt : std::optional<std::string>(note_buf_);
}

bool BranchPanel::reload(DbSession& session, std::string* why) {
  BranchFilter f;
  f.limit = 200;
  auto list = session.branches().list_branches(f);
  if (!list) {
    *why = list.error().message();
    return false;
  }
  rows_ = *list;
  return true;
}

bool BranchPanel::save(DbSession& session, std::string* why) {
  if (draft_.name.empty()) {
    *why = "支派名不能为空";
    return false;
  }
  const bool is_new = draft_.id.empty();
  if (is_new) draft_.id = generate_ulid();
  Result<Branch> saved = is_new ? session.branches().create_branch(draft_)
                                : session.branches().update_branch(draft_);
  if (!saved) {
    // 新建失败时 id 已生成，若不清空会切到"编辑态"，
    // 下次保存走 update 且 id 不存在 → 流程卡死。
    if (is_new) draft_.id.clear();
    *why = saved.error().message();
    return false;
  }
  draft_ = *saved;
  reload(session, why);
  return true;
}

void BranchPanel::select(const Branch& b) {
  draft_ = b;
  name_buf_ = b.name;
  generation_buf_ = b.generation_char.value_or("");
  note_buf_ = b.note.value_or("");
}

void BranchPanel::new_draft() {
  draft_ = Branch{};
  name_buf_.clear();
  generation_buf_.clear();
  note_buf_.clear();
}

void BranchPanel::clear() {
  rows_.clear();
  new_draft();
}

}  // namespace genealogy::gui
