#pragma once

// 关系面板：两人关系路径 / 辈分差 / 五服 / 称谓（M1.3 世系算法的 GUI 出口）。
//
// ego 默认沿用全局选中；alter 由用户在输入框填写 id（暂无跨面板跳转入口）。
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
  // 关库/换库清计算结果（复核 2.13：旧库的路径/五服/称谓不再展示）
  void on_database_closed() override {
    computed_ = false;
    has_path_ = false;
    path_text_.clear();
    path_desc_.clear();
    generation_gap_ = 0;
    ego_gen_ = 0;
    alter_gen_ = 0;
    mourning_garment_.clear();
    mourning_period_.clear();
    mourning_months_ = 0;
    mourning_rule_.clear();
    app_standard_.clear();
    app_colloquial_.clear();
    app_formal_.clear();
    app_basis_.clear();
    app_path_.clear();
    app_elder_ = false;
    app_fallback_ = false;
    via_mother_ = false;
    via_marriage_ = false;
    error_.clear();
  }

 private:
  void compute(DbSession& session);

  // 面板是否持有任何缓存数据（换库清理断言的观测缝）
  bool has_cached_data() const { return computed_; }

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
