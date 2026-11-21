#pragma once

// 校验面板：ValidationIssue 列表，点击定位到实体（设计文档 3.4）。
// 与 CLI io validate 同一引擎——面板把 report 摆到眼前， CLI 给退出码。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

struct IssueRow {
  std::string rule_id;
  std::string severity;   // error / warning / info
  std::string entity_type;
  std::string entity_id;
  std::string message;
  std::string suggestion;
};

class ValidationPanel : public Panel {
 public:
  explicit ValidationPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "校验"; }
  void draw(DbSession& session) override;
  // 关库/换库清结果（复核 2.13：旧库的校验结论不再展示）
  void on_database_closed() override {
    issues_.clear();
    error_count_ = 0;
    warning_count_ = 0;
    info_count_ = 0;
    rules_run_ = 0;
    rules_total_ = 0;
    ran_ = false;
  }

 private:
  void run(DbSession& session);

  AppState& state_;
  std::vector<IssueRow> issues_;
  int error_count_ = 0;
  int warning_count_ = 0;
  int info_count_ = 0;
  int rules_run_ = 0;
  int rules_total_ = 0;
  bool ran_ = false;
};

}  // namespace genealogy::gui
