// 排版导出面板的渲染层（ImGui）。

#include <genealogy/gui/panels/export_panel.hpp>
#include <genealogy/gui/widgets.hpp>
#include <genealogy/gui/svg_preview_draw.hpp>

#include <imgui.h>

#include <algorithm>
#include <string>

namespace genealogy::gui {

namespace {
const char* kPresets[] = {"guji_classic", "plain_modern"};
}

void ExportPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextUnformatted("请先打开数据库");
    return;
  }

  ImGui::SeparatorText("产出");
  const char* kinds[] = {"古籍版式 SVG", "世系图 SVG", "谱书 HTML"};
  ImGui::Combo("类型", &output_kind_, kinds, 3);
  ImGui::SeparatorText("参数");
  static int preset_idx = 0;
  static int chart_style_idx = 1;
  static int depth = 5;
  static char size_buf[16] = "";
  static char title_buf[64] = "";
  if (output_kind_ == 0) {
    ImGui::Combo("预设", &preset_idx, kPresets, 2);
    ImGui::InputText("字号（如 22pt，空=预设）", size_buf, sizeof(size_buf));
    ImGui::InputText("书名", title_buf, sizeof(title_buf));
  } else if (output_kind_ == 1) {
    const char* styles[] = {"祖先图", "后代图", "沙漏图", "五服图", "欧式", "苏式"};
    ImGui::Combo("体例", &chart_style_idx, styles, 6);
    ImGui::SliderInt("代数", &depth, 2, 12);
    ImGui::InputText("书名", title_buf, sizeof(title_buf));
  } else {
    ImGui::TextDisabled("谱书导出沿用 P0 三种版式，字体随包内嵌");
  }

  ImGui::SeparatorText("标记文本（仅古籍版式；可留空）");
  if (ImGui::Button("从当前人物生成")) {
    markup_ = build_markup(session, state_.selected_person_id);
  }
  ImGui::SameLine();
  if (ImGui::Button("清空")) markup_.clear();
  input_text_multiline("##export_markup", &markup_,
                       ImGui::GetTextLineHeight() * 8);
  ImGui::TextDisabled("〔卷〕〔章〕〔注〕〔圈〕〔点〕〔页〕等标记可直接编辑");

  if (ImGui::Button("生成预览", ImVec2(140, 0))) {
    std::string why;
    bool ok = false;
    int layout_pages = 0;
    if (output_kind_ == 0)
      ok = generate_layout(markup_, kPresets[preset_idx], size_buf, title_buf,
                           &preview_, &why, &layout_pages);
    else if (output_kind_ == 1) {
      const char* ids[] = {"pedigree", "descendant", "hourglass", "wufu",
                           "european", "soviet"};
      ok = generate_chart(session, ids[chart_style_idx],
                          state_.selected_person_id, depth, &preview_, &why);
    } else
      ok = generate_book(session, &preview_, &why);
    if (ok) {
      if (output_kind_ == 0 && layout_pages > 1) {
        state_.set_status("共 " + std::to_string(layout_pages) +
                          " 页，预览与单文件导出均为第 1 页；整册请用 "
                          "CLI --export layout（按页拆分落盘）", false);
      }
      if (output_kind_ == 2) {
        scene_ = SvgScene{};  // 谱书是 HTML，不是 SVG：清掉上一次的旧预览
      } else {
        std::string scene_err;
        if (!refresh_scene(&scene_err)) {
          state_.set_status("预览解析失败: " + scene_err, true);
        }
      }
    }
    state_.set_status(ok ? "已生成排版产物（可导出文件）"
                         : "排版失败: " + why, !ok);
  }
  ImGui::SameLine();
  static char path_buf[260] = "family-layout.svg";
  ImGui::InputText("##export_path", path_buf, sizeof(path_buf));
  if (ImGui::Button("导出文件", ImVec2(120, 0))) {
    if (preview_.empty()) {
      state_.set_status("请先生成预览", true);
    } else {
      std::string why;
      if (write_file(path_buf, preview_, &why))
        state_.set_status(std::string("已导出: ") + path_buf);
      else
        state_.set_status(why, true);
    }
  }

  if (!preview_.empty()) {
    ImGui::SeparatorText("产物信息");
    ImGui::Text("SVG 字节数: %zu / 图元 %zu", preview_.size(),
                svg_node_count(scene()));
    ImGui::Checkbox("内嵌预览（ImDrawList 直绘，无光栅化依赖）", &show_preview_);
    if (show_preview_ && !scene().nodes.empty()) {
      ImGui::BeginChild("##svg_preview", ImVec2(0, 420), true,
                        ImGuiWindowFlags_HorizontalScrollbar);
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float scale = std::min(avail.x / scene().width,
                                   avail.y / scene().height);
      const ImVec2 size(scene().width * scale, scene().height * scale);
      const ImVec2 cur = ImGui::GetCursorScreenPos();
      ImGui::Dummy(size);
      draw_svg_preview(scene(), ImGui::GetWindowDrawList(), cur, scale);
      ImGui::EndChild();
    }
  }
}

}  // namespace genealogy::gui
