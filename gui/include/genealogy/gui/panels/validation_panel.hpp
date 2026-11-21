#pragma once

// 校验面板：ValidationIssue 列表，点击定位到实体（设计文档 3.4）。
// 与 CLI io validate 同一引擎——面板把 report 摆到眼前， CLI 给退出码。
//
// 校验异步化（第三轮新-C10）：全库装载 + 全规则跑是秒级操作，同步跑
// 在 draw() 里整帧冻结。改后台线程 + 代际守卫：结果落 pending 缓冲、
// running_ 翻转为发布点，UI 线程采纳；换库/重跑作废在途批次。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <atomic>
#include <string>
#include <thread>
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
  ~ValidationPanel() override;

  const char* title() const override { return "校验"; }
  void draw(DbSession& session) override;
  // 关库/换库清结果（复核 2.13：旧库的校验结论不再展示）；在跑的
  // 后台校验先停（第三轮新-C10）
  void on_database_closed() override;

 private:
  void stop_worker();

  AppState& state_;
  std::vector<IssueRow> issues_;
  int error_count_ = 0;
  int warning_count_ = 0;
  int info_count_ = 0;
  int rules_run_ = 0;
  int rules_total_ = 0;
  bool ran_ = false;

  // 后台校验（第三轮新-C10）：worker 独占写 pending，写完翻转
  // running_（release）；UI 线程 running_ == false 后读（acquire）
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::vector<IssueRow> pending_;
  int pending_error_ = 0;
  int pending_warning_ = 0;
  int pending_info_ = 0;
  int pending_rules_run_ = 0;
  int pending_rules_total_ = 0;
  std::string pending_status_;   // 成功摘要或失败原因（空串区分两种态）
  bool pending_ok_ = false;
  bool pending_ready_ = false;   // UI 线程置 false 表示已采纳
};

}  // namespace genealogy::gui
