#include <genealogy/gui/panels/person_table.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstring>

#include <genealogy/model/person.hpp>

namespace genealogy::gui {

namespace {

// 数值列的安全解析：脏库里出现非数字内容时不能让 std::stoi 从 ImGui
// 绘制回调抛出（无人捕获 → 进程终止），回退 0 与空串同义。
int safe_int(const std::string& s) {
  if (s.empty()) return 0;
  try {
    std::size_t used = 0;
    const int v = std::stoi(s, &used);
    return used == s.size() ? v : 0;
  } catch (const std::exception&) {
    return 0;
  }
}

const char* gender_label(Gender g) {
  switch (g) {
    case Gender::Male: return "男";
    case Gender::Female: return "女";
    case Gender::Unknown: break;
  }
  return "?";
}

const char* privacy_label(PrivacyLevel p) {
  switch (p) {
    case PrivacyLevel::Public: return "公开";
    case PrivacyLevel::Kin: return "亲缘";
    case PrivacyLevel::Private: return "私密";
  }
  return "?";
}

}  // namespace

void PersonTablePanel::reload(DbSession& session) {
  rows_.clear();
  loaded_db_path_ = state_.db_path;   // 换库即重载（路径变 = 库变）
  PersonFilter pf;
  pf.limit = 200000;
  auto got = session.persons().list_persons(pf);
  if (!got) {
    state_.set_status("载入人物失败: " + got.error().message(), true);
    loaded_ = false;
    return;
  }
  rows_.reserve(got->size());
  for (const auto& p : *got) {
    TableRow r;
    r.id = p.id;
    r.family_name = p.family_name;
    r.given_name = p.given_name;
    r.courtesy_name = p.courtesy_name.value_or("");
    r.gender = gender_label(p.gender);
    r.birth = p.birth_date.is_empty() ? "" : p.birth_date.display();
    r.death = (p.death_date && !p.death_date->is_empty()) ? p.death_date->display()
                                                          : "";
    r.generation = p.generation_no ? std::to_string(*p.generation_no) : "";
    r.birth_order = p.birth_order ? std::to_string(*p.birth_order) : "";
    r.privacy = privacy_label(p.privacy);
    r.father_id = p.father_id.value_or("");
    r.mother_id = p.mother_id.value_or("");
    rows_.push_back(std::move(r));
  }
  loaded_ = true;
}

void PersonTablePanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }
  if (!loaded_ || loaded_db_path_ != state_.db_path) {
    reload(session);
    if (!loaded_) return;
  }

  // ---- 筛选条 ----
  ImGui::SetNextItemWidth(200);
  ImGui::InputText("姓名", name_filter_, sizeof(name_filter_));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("世代≥", &gen_from_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("世代≤", &gen_to_);
  ImGui::SameLine();
  if (ImGui::Button("重新载入")) {
    reload(session);
  }
  ImGui::SameLine();
  ImGui::Text("%zu 人", rows_.size());

  // ---- 表格 ----
  // ImGuiTable 的列排序自己实现（TableSortSpecs 只给方向，排序要自己做）。
  static const int kColCount = 10;
  static const char* kColNames[kColCount] = {
      "世代", "行几", "姓", "名", "字", "性别", "生", "卒", "隐私", "id"};
  const ImGuiTableFlags flags =
      ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
      ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
      ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
      ImGuiTableFlags_ScrollY;
  if (!ImGui::BeginTable("persons", kColCount, flags)) return;

  ImGui::TableSetupColumn(kColNames[0],
                          ImGuiTableColumnFlags_DefaultSort |
                              ImGuiTableColumnFlags_WidthFixed,
                          50);
  ImGui::TableSetupColumn(kColNames[1], ImGuiTableColumnFlags_WidthFixed, 45);
  ImGui::TableSetupColumn(kColNames[2], ImGuiTableColumnFlags_WidthFixed, 80);
  ImGui::TableSetupColumn(kColNames[3], ImGuiTableColumnFlags_WidthFixed, 90);
  ImGui::TableSetupColumn(kColNames[4], ImGuiTableColumnFlags_WidthFixed, 100);
  ImGui::TableSetupColumn(kColNames[5], ImGuiTableColumnFlags_WidthFixed, 45);
  ImGui::TableSetupColumn(kColNames[6], ImGuiTableColumnFlags_WidthFixed, 100);
  ImGui::TableSetupColumn(kColNames[7], ImGuiTableColumnFlags_WidthFixed, 100);
  ImGui::TableSetupColumn(kColNames[8], ImGuiTableColumnFlags_WidthFixed, 55);
  ImGui::TableSetupColumn(kColNames[9], ImGuiTableColumnFlags_WidthFixed, 190);
  ImGui::TableSetupScrollFreeze(0, 1);   // 表头固定
  ImGui::TableHeadersRow();

  // 排序（点表头）
  if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
    if (specs->SpecsDirty) {
      sort_col_ = specs->Specs[0].ColumnIndex;
      sort_asc_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
      specs->SpecsDirty = false;
      const auto key_of = [](const TableRow& r, int col) -> std::string {
        switch (col) {
          case 0: return r.generation;
          case 1: return r.birth_order;
          case 2: return r.family_name;
          case 3: return r.given_name;
          case 4: return r.courtesy_name;
          case 5: return r.gender;
          case 6: return r.birth;
          case 7: return r.death;
          case 8: return r.privacy;
          default: return r.id;
        }
      };
      std::stable_sort(rows_.begin(), rows_.end(),
                       [&](const TableRow& a, const TableRow& b) {
                         const std::string ka = key_of(a, sort_col_);
                         const std::string kb = key_of(b, sort_col_);
                         // 世代/行几按数值语义比：字符串比会让 "10" < "9"
                         const bool numeric = sort_col_ == 0 || sort_col_ == 1;
                         const int na = numeric ? safe_int(ka) : 0;
                         const int nb = numeric ? safe_int(kb) : 0;
                         const bool less = numeric ? na < nb : ka < kb;
                         // 降序必须沿同一口径取反；此前 kb < ka 让数值列
                         // 退化为字符串比较（"9" 排在 "10" 之前）。
                         return sort_asc_ ? less
                                          : (numeric ? nb < na : kb < ka);
                       });
    }
  }

  // 行遍历 + 筛选
  const bool has_name = name_filter_[0] != '\0';
  for (const auto& r : rows_) {
    if (has_name) {
      const std::string full = r.family_name + r.given_name + r.courtesy_name;
      if (full.find(name_filter_) == std::string::npos) continue;
    }
    const int gen = safe_int(r.generation);
    if (gen_from_ != 0 && gen < gen_from_) continue;
    if (gen_to_ != 0 && gen > gen_to_) continue;
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.generation.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.birth_order.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.family_name.c_str());
    ImGui::TableNextColumn();
    // 姓名这一列做成可选中的按钮（点选 → 人物卡联动）
    if (ImGui::SmallButton(r.given_name.c_str())) {
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        std::string why;
        if (!begin_edit(r.id, &why)) state_.set_status(why, true);
      } else {
        state_.selected_person_id = r.id;
      }
    }
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.courtesy_name.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.gender.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.birth.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.death.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.privacy.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.id.c_str());
  }
  ImGui::EndTable();

  // ---- 行内编辑区（双击姓名列进入）
  if (editing()) {
    ImGui::SeparatorText(("编辑人物：" + edit_id()).c_str());
    ImGui::SetNextItemWidth(100);
    input_text("姓", &efamily_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    input_text("名", &egiven_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    input_text("字", &ecourtesy_);
    ImGui::RadioButton("男", &egender_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("女", &egender_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("未知", &egender_, 2);
    ImGui::SetNextItemWidth(80);
    input_text("世代", &egen_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    input_text("行几", &ebirth_order_);
    if (ImGui::Button("保存", ImVec2(100, 0))) {
      std::string why;
      if (!save_edit(session, &why)) state_.set_status("保存失败: " + why, true);
      else state_.set_status("人物已保存");
    }
    ImGui::SameLine();
    if (ImGui::Button("取消")) cancel_edit();
  }
}

}  // namespace genealogy::gui
