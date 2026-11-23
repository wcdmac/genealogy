// 备份恢复面板的渲染层。

#include <genealogy/gui/panels/backup_panel.hpp>
#include <genealogy/gui/widgets.hpp>

#include <imgui.h>

namespace genealogy::gui {

void BackupPanel::draw(DbSession& session) {
  if (!ImGui::Begin(title(), &open)) { ImGui::End(); return; }
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("备份（一致性快照 VACUUM INTO）");
  ImGui::SetNextItemWidth(300);
  input_text("备份目录", &dir_buf_);
  if (ImGui::Button("立即备份", ImVec2(140, 0))) {
    std::string path, why;
    if (!backup(session, dir_buf_, &path, &why))
      state_.set_status("备份失败: " + why, true);
    else
      state_.set_status("已备份到: " + path);
  }
  ImGui::TextDisabled("当前库: %s", session.path().c_str());
  ImGui::TextDisabled("附件目录需另行备份（本面板只处理主库）");

  ImGui::SeparatorText("恢复（默认拒绝覆盖）");
  ImGui::SetNextItemWidth(400);
  input_text("快照文件", &snap_buf_);
  ImGui::Checkbox("确认覆盖已存在的目标库（旧库移为 .bak）", &force_);
  if (ImGui::Button("从快照恢复", ImVec2(160, 0))) {
    std::string moved, why;
    if (!restore_and_reopen(session, snap_buf_, force_, &moved, &why))
      state_.set_status("恢复失败: " + why, true);
    else
      state_.set_status("已恢复；旧库: " + (moved.empty() ? "无" : moved));
  }
  ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.3F, 1.0F),
                     "恢复会替换当前库的磁盘文件；请先关闭其他使用该库的程序。");
  ImGui::End();
}

}  // namespace genealogy::gui
