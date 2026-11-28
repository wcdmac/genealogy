// 人物卡面板的渲染层（ImGui）。状态机（load/save/切换决策）在
// person_card_state.cpp——无 imgui 依赖、可被 gui-tests 无头驱动；
// 本 TU 只把状态机的 API 画出来，不含任何装配逻辑。

#include <genealogy/gui/panels/person_card.hpp>

#include <imgui.h>

#include <genealogy/gui/widgets.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

namespace {

const char* kGenderLabels[] = {"男", "女", "未知"};
const char* kPrivacyLabels[] = {"公开", "亲缘", "私密"};

}  // namespace

void PersonCardPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }
  if (state_.selected_person_id.empty()) {
    ImGui::TextDisabled("（在检索/世系表/世系树里点选一个人物）");
    return;
  }
  // 选中变了就重新装载（跨面板联动的落点）。有未保存修改时不静默
  // 覆盖（第三轮新-C3）：面板内显式三选——保存/放弃/留在当前。
  if (state_.selected_person_id != loaded_id_) {
    // 读取失败的人物不再每帧重试（第四轮 P3），重选其他人物即恢复
    if (loaded_id_.empty() && state_.selected_person_id == failed_load_id_) {
      ImGui::TextDisabled("该人物读取失败（详见状态栏）；重新选择人物继续。");
      return;
    }
    if (switch_pending(state_.selected_person_id)) {
      ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.45F, 1.0F),
                         "人物卡有未保存修改，全局选中已切到 %.16s…",
                         state_.selected_person_id.c_str());
      if (ImGui::Button("保存并切换")) {
        resolve_switch(SwitchAction::SaveAndLoad, session);
      }
      ImGui::SameLine();
      if (ImGui::Button("放弃修改并切换")) {
        resolve_switch(SwitchAction::DiscardAndLoad, session);
      }
      ImGui::SameLine();
      if (ImGui::Button("留在当前")) {
        resolve_switch(SwitchAction::Stay, session);
      }
      return;  // 确认期间不渲染编辑区，避免继续累积未保存修改
    }
    load(session);
    if (loaded_id_.empty()) return;
  }

  ImGui::SeparatorText("姓名");
  ImGui::SetNextItemWidth(120);
  if (input_text("姓", &buf_family_, 64)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  if (input_text("名", &buf_given_, 64)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  if (ImGui::Combo("性别", &buf_gender_, kGenderLabels, 3)) mark_edited();

  ImGui::SetNextItemWidth(140);
  if (input_text("字", &buf_courtesy_, 64)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  if (input_text("号", &buf_art_, 64)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  if (input_text("谥/赠", &buf_posthumous_, 64)) mark_edited();

  ImGui::SetNextItemWidth(180);
  if (input_text("别称/改名前名", &buf_alias_, 96)) mark_edited();
  // 名号变更史（M7.1 验收门：人物卡显示全部名号）——只读列表，
  // 增删走 genea person name / REST /persons/{id}/names
  if (!name_rows_.empty()) {
    ImGui::SeparatorText("名号");
    for (const auto& row : name_rows_) {
      ImGui::TextUnformatted(row.c_str());
    }
  }

  ImGui::SeparatorText("生卒");
  ImGui::SetNextItemWidth(120);
  if (input_text("生（公历）", &buf_birth_, 32)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  if (input_text("卒（公历）", &buf_death_, 32)) mark_edited();
  ImGui::SameLine();
  if (ImGui::Checkbox("在世", &buf_living_)) mark_edited();
  ImGui::SameLine();
  if (ImGui::Checkbox("承重/嫡嗣", &buf_heir_)) mark_edited();

  ImGui::SeparatorText("行辈与隐私");
  ImGui::SetNextItemWidth(80);
  if (input_text("行几", &buf_birth_order_, 8)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  if (input_text("世代", &buf_generation_no_, 8)) mark_edited();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  if (ImGui::Combo("隐私", &buf_privacy_, kPrivacyLabels, 3)) mark_edited();

  ImGui::SeparatorText("行传");
  if (input_text_multiline("##biography", &bio_buf_,
                           ImGui::GetTextLineHeight() * 6)) {
    mark_edited();
  }

  ImGui::SeparatorText("备注");
  if (input_text_multiline("##note", &note_buf_,
                           ImGui::GetTextLineHeight() * 3)) {
    mark_edited();
  }

  if (dirty_) {
    ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.2F, 1.0F), "有未保存修改");
    ImGui::SameLine();
  }
  if (ImGui::Button("保存")) {
    save(session);
  }
  ImGui::SameLine();
  if (ImGui::Button("重新载入")) {
    load(session);
  }
  ImGui::SameLine();
  ImGui::Text("rev=%d", loaded_rev_);

  // 关系链：点了就换全局选中，人物卡自己会跟着换
  ImGui::SeparatorText("关系");
  auto link = [&](const char* label, const std::string& id) {
    if (id.empty()) {
      ImGui::Text("%s: —", label);
      return;
    }
    ImGui::PushID(id.c_str());
    if (ImGui::SmallButton(label)) {
      state_.selected_person_id = id;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", id.c_str());
    ImGui::PopID();
  };
  link("父", father_id_);
  link("母", mother_id_);
  for (const auto& id : spouse_ids_) link("配偶", id);
  for (const auto& id : child_ids_) link("子", id);

  // 关系快捷录入（借鉴 Gramps 的关系按钮）：为当前焦点加父/母/配偶/子女。
  // 设置 AppState 的待建关系，新建面板会自动预填并打开。
  ImGui::SeparatorText("快捷建关系");
  auto quick = [&](const char* label, const char* kind) {
    if (ImGui::SmallButton(label)) {
      state_.new_relation_kind = kind;
      state_.new_relation_anchor = state_.selected_person_id;
    }
    ImGui::SameLine();
  };
  quick("+父亲", "father");
  quick("+母亲", "mother");
  quick("+配偶", "spouse");
  quick("+子女", "child");
  ImGui::NewLine();
}

}  // namespace genealogy::gui