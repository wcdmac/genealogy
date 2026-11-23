#pragma once

// 排版导出面板：把 core 的排版管线接入 GUI（ADR-1，不在 GUI 重写排版）。
// 三种产出：世系图 SVG（chart）、谱书 HTML（book）、古籍版式 SVG（layout）。
// 数据装配在 export_panel_state.cpp（无 ImGui 依赖，可无头测试）。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>
#include <genealogy/gui/svg_preview.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class ExportPanel : public Panel {
 public:
  explicit ExportPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "排版导出"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { preview_.clear(); }
  bool has_cached_data() const override { return !preview_.empty(); }

  // ---- 无头状态机 API（定义在 export_panel_state.cpp）
  // 从数据库与焦点人物生成排版所用标记文本（姓名、行传 → 注文）
  std::string build_markup(DbSession& session, const std::string& focus_id) const;
  // 生成古籍版式 SVG（preset: guji_classic|plain_modern，body_size 可空）
  bool generate_layout(const std::string& markup, const std::string& preset,
                       const std::string& body_size, const std::string& title,
                       std::string* out_svg, std::string* why) const;
  // 生成世系图 SVG（style: pedigree/descendant/hourglass/wufu/european/soviet）
  bool generate_chart(DbSession& session, const std::string& style,
                      const std::string& focus_id, int depth,
                      std::string* out_svg, std::string* why) const;
  // 生成谱书 HTML（含字体 base64，产物较大）
  bool generate_book(DbSession& session, std::string* out_html,
                     std::string* why) const;
  // 写文件（成功 true）
  bool write_file(const std::string& path, const std::string& content,
                  std::string* why) const;

  void set_markup(const std::string& m) { markup_ = m; }
  const std::string& markup() const { return markup_; }
  void set_preview(const std::string& p) { preview_ = p; }
  const std::string& preview() const { return preview_; }
  // 解析当前预览 SVG 为可绘制场景（生成后调用）
  bool refresh_scene(std::string* why);
  const SvgScene& scene() const { return scene_; }
  int output_kind() const { return output_kind_; }
  void set_output_kind(int k) { output_kind_ = k; }

 private:
  AppState& state_;
  std::string markup_;
  std::string preview_;
  SvgScene scene_;
  bool show_preview_ = true;
  int output_kind_ = 0;  // 0 古籍版式 / 1 世系图 / 2 谱书
};

}  // namespace genealogy::gui
