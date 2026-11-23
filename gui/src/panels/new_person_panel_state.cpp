// 新建人物面板的无 UI 状态机。
// 装配与创建走 PersonService（ADR-1），不直连仓储；gui-tests 无头驱动。

#include <genealogy/gui/panels/new_person_panel.hpp>

#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>

#include <algorithm>

namespace genealogy::gui {

namespace {

// 输入是公历 ISO（数字与 -）还是老谱原文（含汉字）
bool looks_iso(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') return false;
  return true;
}

Result<GenealogyDate> parse_date(const std::string& s) {
  return looks_iso(s) ? GenealogyDate::from_iso(s) : GenealogyDate::from_raw(s);
}

}  // namespace

void NewPersonPanel::prefill(DbSession& session, const std::string& focus_id) {
  // 关系快捷：人物卡「加子女」等按钮设置了锚点与类型
  if (!state_.new_relation_kind.empty() &&
      !state_.new_relation_anchor.empty()) {
    pending_kind_ = state_.new_relation_kind;
    pending_anchor_ = state_.new_relation_anchor;
    const auto& kind = pending_kind_;
    const auto& anchor = pending_anchor_;
    auto person = session.persons().get_person(anchor);
    if (person) {
      if (draft_.family_name.empty()) draft_.family_name = person->family_name;
      if (kind == "child") {
        // 焦点为父或母：按性别决定填 father_id / mother_id
        if (person->gender == Gender::Female) draft_.mother_id = anchor;
        else draft_.father_id = anchor;
      } else if (kind == "father" || kind == "mother") {
        // 新建的是焦点的父/母：新人物性别相反，暂不回填到焦点
        // （修改焦点人物字段属修改流，避免误覆盖）
      }
      prefill_from_id_ = anchor;
    }
    state_.new_relation_kind.clear();
    state_.new_relation_anchor.clear();
    return;
  }
  if (focus_id.empty() || focus_id == prefill_from_id_) return;
  prefill_from_id_ = focus_id;
  auto focus = session.persons().get_person(focus_id);
  if (!focus) return;
  // 智能预填：姓氏随焦点、世代默认下一代、父亲为焦点（若焦点为男性）。
  if (draft_.family_name.empty()) draft_.family_name = focus->family_name;
  if (draft_.generation_no.empty() && focus->generation_no)
    draft_.generation_no = std::to_string(*focus->generation_no + 1);
  if (focus->gender == Gender::Male) draft_.father_id = focus_id;
  // 已有姓名时不覆盖（用户可能正在输入）
}

bool NewPersonPanel::create(DbSession& session) {
  if (draft_.given_name.empty()) {
    state_.set_status("姓名（名）不能为空", true);
    return false;
  }
  Person p;
  p.family_name = draft_.family_name;
  p.given_name = draft_.given_name;
  if (!draft_.courtesy_name.empty()) p.courtesy_name = draft_.courtesy_name;
  p.gender = static_cast<Gender>(draft_.gender);
  if (!draft_.father_id.empty()) p.father_id = draft_.father_id;
  if (!draft_.mother_id.empty()) p.mother_id = draft_.mother_id;
  if (!draft_.generation_no.empty()) {
    try { p.generation_no = std::stoi(draft_.generation_no); } catch (...) {}
  }
  if (!draft_.biography.empty()) p.biography = draft_.biography;
  if (!draft_.note.empty()) p.note = draft_.note;

  if (!draft_.birth_date.empty()) {
    auto d = parse_date(draft_.birth_date);
    if (!d) {
      state_.set_status("出生日期无法识别: " + draft_.birth_date, true);
      return false;
    }
    p.birth_date = *d;
  }
  if (!draft_.death_date.empty()) {
    auto d = parse_date(draft_.death_date);
    if (!d) {
      state_.set_status("卒期无法识别: " + draft_.death_date, true);
      return false;
    }
    p.death_date = *d;
  }

  auto made = session.persons().create_person(p);
  if (!made) {
    state_.set_status("创建失败: " + made.error().message(), true);
    return false;
  }
  // 关系快捷真正落地（不只是预填）
  if (!pending_kind_.empty() && !pending_anchor_.empty()) {
    if (pending_kind_ == "spouse") {
      Union u;
      u.type = UnionType::Marriage;
      u.members.push_back({pending_anchor_, MemberRole::Husband});
      u.members.push_back({made->id, MemberRole::Wife});
      auto un = session.unions().create_union(u);
      if (!un)
        state_.set_status("人物已建，但婚姻创建失败: " + un.error().message(), true);
    } else if (pending_kind_ == "father" || pending_kind_ == "mother") {
      // 新建的是锚点的父/母：回填锚点的 father_id / mother_id
      auto anchor = session.persons().get_person(pending_anchor_);
      if (anchor) {
        if (pending_kind_ == "father") anchor->father_id = made->id;
        else anchor->mother_id = made->id;
        auto upd = session.persons().update_person(*anchor);
        if (!upd)
          state_.set_status("人物已建，但回填关系失败: " + upd.error().message(), true);
      }
    }
    pending_kind_.clear();
    pending_anchor_.clear();
    state_.new_relation_kind.clear();
    state_.new_relation_anchor.clear();
  }
  state_.selected_person_id = made->id;
  if (!state_.status_is_error)
    state_.set_status("已创建: " + made->family_name + made->given_name);
  // 保留姓氏/世代/父亲预填（连续录入同一支），清一次性字段
  draft_.given_name.clear();
  draft_.courtesy_name.clear();
  draft_.birth_date.clear();
  draft_.death_date.clear();
  draft_.biography.clear();
  draft_.note.clear();
  prefill_from_id_ = made->id;  // 下一人默认父亲为刚建者？不——仍随焦点
  prefill_from_id_.clear();
  return true;
}

}  // namespace genealogy::gui
