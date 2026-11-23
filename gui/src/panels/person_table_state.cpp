// 人物表格的行内编辑状态机（无 ImGui 依赖，可无头测）。

#include <genealogy/gui/panels/person_table.hpp>

#include <genealogy/model/person.hpp>

namespace genealogy::gui {

bool PersonTablePanel::begin_edit(const std::string& id, std::string* why) {
  // 只在当前载入的 rows_ 中找——避免凭空 id 触发数据库查询
  const TableRow* found = nullptr;
  for (const auto& r : rows_)
    if (r.id == id) { found = &r; break; }
  if (found == nullptr) { *why = "人物不在当前表格中"; return false; }
  edit_id_ = id;
  efamily_ = found->family_name;
  egiven_ = found->given_name;
  ecourtesy_ = found->courtesy_name;
  egen_ = found->generation;
  ebirth_order_ = found->birth_order;
  egender_ = (found->gender == "男") ? 0 : (found->gender == "女") ? 1 : 2;
  return true;
}

bool PersonTablePanel::save_edit(DbSession& session, std::string* why) {
  if (edit_id_.empty()) { *why = "未进入编辑态"; return false; }
  if (egiven_.empty()) { *why = "名不能为空"; return false; }
  auto current = session.persons().get_person(edit_id_);
  if (!current) { *why = current.error().message(); return false; }
  Person p = *current;
  p.family_name = efamily_;
  p.given_name = egiven_;
  p.courtesy_name = ecourtesy_.empty()
      ? std::nullopt : std::optional<std::string>(ecourtesy_);
  p.gender = static_cast<Gender>(egender_);
  if (egen_.empty()) p.generation_no = std::nullopt;
  else {
    try { p.generation_no = std::stoi(egen_); } catch (...) {
      *why = "世代须为整数"; return false;
    }
  }
  if (ebirth_order_.empty()) p.birth_order = std::nullopt;
  else {
    try { p.birth_order = std::stoi(ebirth_order_); } catch (...) {
      *why = "行几须为整数"; return false;
    }
  }
  // update_person 内部按 rev 做乐观锁，冲突由错误带回状态栏
  auto saved = session.persons().update_person(p);
  if (!saved) { *why = saved.error().message(); return false; }
  state_.selected_person_id = edit_id_;
  cancel_edit();
  reload(session);
  edit_rev_ = 0;
  return true;
}

void PersonTablePanel::cancel_edit() {
  edit_id_.clear();
  efamily_.clear(); egiven_.clear(); ecourtesy_.clear();
  egen_.clear(); ebirth_order_.clear();
  egender_ = 2;
  edit_rev_ = 0;
}

}  // namespace genealogy::gui
