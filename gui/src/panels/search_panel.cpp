#include <genealogy/gui/panels/search_panel.hpp>

#include <imgui.h>

#include <string>

#include <genealogy/model/person.hpp>

namespace genealogy::gui {

void SearchPanel::run_search(DbSession& session) {
  hits_.clear();
  const std::string q(query_);
  if (q.empty()) {
    state_.set_status("请输入检索词", true);
    searched_ = false;
    return;
  }
  auto got = session.persons().search_persons(q, limit_);
  if (!got) {
    state_.set_status("检索失败: " + got.error().message(), true);
    searched_ = false;
    return;
  }
  hits_.reserve(got->size());
  for (const auto& p : *got) {
    SearchHit h;
    h.id = p.id;
    h.name = p.family_name + p.given_name;
    h.gender = p.gender == Gender::Male    ? "男"
               : p.gender == Gender::Female ? "女"
                                            : "?";
    h.birth = p.birth_date.is_empty() ? "" : p.birth_date.display();
    h.death = (p.death_date && !p.death_date->is_empty()) ? p.death_date->display()
                                                          : "";
    h.generation = p.generation_no.value_or(0);
    hits_.push_back(std::move(h));
  }
  searched_ = true;
  state_.set_status("命中 " + std::to_string(hits_.size()) + " 人");
}

void SearchPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }

  ImGui::SetNextItemWidth(280);
  // InputText with hint：回车直接搜（谱师输入姓名后习惯回车）
  const bool enter =
      ImGui::InputTextWithHint("##q", "姓名 / 字 / 号 / 行传关键词", query_,
                               sizeof(query_), ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("检索") || enter) {
    run_search(session);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("上限", &limit_);
  if (limit_ < 1) limit_ = 1;
  if (limit_ > 10000) limit_ = 10000;

  ImGui::Separator();

  if (!searched_) {
    ImGui::TextDisabled("输入关键词后回车或点「检索」。FTS 走 SQLite 全文索引。");
    return;
  }
  if (hits_.empty()) {
    ImGui::TextDisabled("无命中");
    return;
  }

  if (ImGui::BeginTable("hits", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("姓名");
    ImGui::TableSetupColumn("性别", ImGuiTableColumnFlags_WidthFixed, 45);
    ImGui::TableSetupColumn("生", ImGuiTableColumnFlags_WidthFixed, 100);
    ImGui::TableSetupColumn("卒", ImGuiTableColumnFlags_WidthFixed, 100);
    ImGui::TableSetupColumn("世代", ImGuiTableColumnFlags_WidthFixed, 50);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (const auto& h : hits_) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::SmallButton(h.name.c_str())) {
        state_.selected_person_id = h.id;   // 点选 → 人物卡联动
      }
      ImGui::TableNextColumn(); ImGui::TextUnformatted(h.gender.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(h.birth.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(h.death.c_str());
      ImGui::TableNextColumn(); ImGui::Text("%d", h.generation);
    }
    ImGui::EndTable();
  }
}

}  // namespace genealogy::gui
