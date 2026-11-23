// 重复合并面板渲染层（并排对照 + 冲突高亮）。

#include <genealogy/gui/panels/merge_panel.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

namespace genealogy::gui {

void MergePanel::draw(DbSession& session) {
  if (!ImGui::Begin(title(), &open)) { ImGui::End(); return; }
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    ImGui::End();
    return;
  }
  if (ImGui::Button("扫描重复人物")) {
    std::string why;
    if (!scan(session, &why)) state_.set_status("扫描失败: " + why, true);
    else state_.set_status("发现 " + std::to_string(pairs_.size()) + " 对疑似重复");
  }

  ImGui::SeparatorText(("疑似重复（共 " + std::to_string(pairs_.size()) + " 对）").c_str());
  ImGui::BeginChild("##pairs", ImVec2(0, 160), true);
  for (int i = 0; i < static_cast<int>(pairs_.size()); ++i) {
    const auto& p = pairs_[i];
    ImGui::PushID(i);
    if (ImGui::Selectable((p.winner_name + "  ⇄  " + p.loser_name).c_str(),
                          i == selected_)) {
      selected_ = i;
      std::string why;
      preview(session, p.winner_id, p.loser_id, &why);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("[%s] %s", genealogy::to_string(p.confidence),
                        p.reason.c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();

  if (selected_ >= 0 && selected_ < static_cast<int>(pairs_.size())) {
    const auto& pair = pairs_[selected_];
    ImGui::SeparatorText("并排对照（左：保留 winner ／ 右：被合并 loser）");
    ImGui::Columns(2, "##compare", false);
    ImGui::Text("保留：%s", pair.winner_name.c_str()); ImGui::NextColumn();
    ImGui::Text("被合并：%s", pair.loser_name.c_str()); ImGui::NextColumn();
    ImGui::TextDisabled("id: %s", pair.winner_id.c_str()); ImGui::NextColumn();
    ImGui::TextDisabled("id: %s", pair.loser_id.c_str()); ImGui::NextColumn();
    ImGui::Columns(1);

    if (!conflicts_.empty()) {
      ImGui::SeparatorText("字段冲突（合并前须人工核对）");
      for (const auto& c : conflicts_)
        ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.3F, 1.0F),
                           "%s：保留=%s ／ 被合并=%s", c.field.c_str(),
                           c.winner_value.c_str(), c.loser_value.c_str());
    }
    static char dir_buf[260] = "backups";
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("##merge_backup_dir", dir_buf, sizeof(dir_buf));
    ImGui::SameLine();
    if (ImGui::Button("执行合并（先自动留快照）")) {
      std::string why;
      if (!merge(session, dir_buf, &why))
        state_.set_status("合并失败: " + why, true);
      else
        state_.set_status("已合并；快照: " + backup_path());
    }
  }
  ImGui::TextDisabled("合并会迁移父子/配偶/事件/名号引用，再删除被合并者。");
  ImGui::End();
}

}  // namespace genealogy::gui
