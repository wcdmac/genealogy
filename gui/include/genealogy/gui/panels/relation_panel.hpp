#pragma once

// 关系面板：两人关系路径 / 辈分差 / 五服 / 称谓（M1.3 世系算法的 GUI 出口）。
//
// ego 默认沿用全局选中，alter 手输 id 或从人物卡/检索跳转过来。
// 这是 GUI 与 CLI 的 person relation 命令等价物——核心价值是「点两下
// 就知道该怎么称呼」，比敲命令行快，但底层算的是同一套算法。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>

namespace genealogy::gui {

class RelationPanel : public Panel {
 public:
  explicit RelationPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "关系"; }
  void draw(DbSession& session) override;

 private:
  void compute(DbSession& session);

  AppState& state_;
  bool computed_ = false;
  bool has_path_ = false;
  // 结果缓冲
  std::string path_text_;      // 如 "H.F"
  std::string path_desc_;      // 步骤文字描述
  int generation_gap_ = 0;     // 辈分差
  int ego_gen_ = 0;            // 世代序号
  int alter_gen_ = 0;
  std::string mourning_garment_;
  std::string mourning_period_;
  int mourning_months_ = 0;
  std::string mourning_rule_;
  std::string app_standard_;
  std::string app_colloquial_;
  std::string app_formal_;
  std::string app_basis_;
  std::string app_path_;
  bool app_elder_ = false;
  bool app_fallback_ = false;
  bool via_mother_ = false;    // 路径经过母亲边
  bool via_marriage_ = false;  // 路径经过婚姻边（姻亲）
  std::string error_;
};

}  // namespace genealogy::gui
