// 校验面板的无 UI 状态机（ROADMAP「面板装配抽离 ImGui TU」债务偿还）。
//
// 装载 + 全规则跑（第三轮新-C10 的后台化）在此成 TU：无 imgui 依赖，
// gui-tests 直接驱动 start_run/poll——后台校验的发布/采纳/换库作废
// 语义自此可自动验证。draw() 留在 validation_panel.cpp（渲染层）。
//
// 线程模型：worker 独占写 pending 缓冲，写完翻转 running_（release）；
// UI 线程 running_ == false 后读（acquire）。AppState 只在 UI/测试
// 线程写（set_status 由 poll 代劳）。

#include <genealogy/gui/panels/validation_panel.hpp>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/validate/validation.hpp>

#include <optional>
#include <string>
#include <utility>
#include <thread>
#include <vector>

namespace genealogy::gui {

// 后台校验体：装载 + 全规则跑，结果写进 pending 缓冲。失败不抛出，
// 原因进 pending_status_（与 CLI io validate 的错误路径同口径）。
// state_.set_status 由 UI 线程在采纳时调用——AppState 不是线程安全的，
// worker 不碰。
static void run_validation_worker(DbSession& session,
                                  std::vector<IssueRow>* out_issues,
                                  int* out_error, int* out_warning,
                                  int* out_info, int* out_rules_run,
                                  int* out_rules_total, std::string* out_status,
                                  bool* out_ok) {
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = session.persons().list_persons(pf);
  if (!persons) {
    *out_status = "载入人物失败: " + persons.error().message();
    *out_ok = false;
    return;
  }
  UnionFilter uf;
  uf.limit = 200000;
  auto unions = session.unions().list_unions(uf);
  if (!unions) {
    *out_status = "载入结合失败: " + unions.error().message();
    *out_ok = false;
    return;
  }
  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(std::move(*persons), std::move(*unions));

  EventRepository events(session.db());
  EventFilter ef;
  ef.limit = 300000;
  auto evs = events.list(ef);
  if (!evs) {
    *out_status = "载入事件失败: " + evs.error().message();
    *out_ok = false;
    return;
  }

  // R16/R17 需要全量 citation：翻页载到尽（与 CLI/REST 同口径）
  CitationRepository citations(session.db());
  std::vector<Citation> citation_rows;
  std::optional<std::string> cite_cursor;
  for (;;) {
    CitationFilter cf;
    cf.limit = 1000;
    cf.cursor = cite_cursor;
    auto page = citations.list(cf);
    if (!page) {
      *out_status = "载入证据引用失败: " + page.error().message();
      *out_ok = false;
      return;
    }
    if (page->empty()) break;
    for (auto& c : *page) citation_rows.push_back(std::move(c));
    cite_cursor = citation_rows.back().id;
  }

  auto report = validate(graph, &*evs, &citation_rows, ValidationOptions{});
  if (!report) {
    *out_status = "校验失败: " + report.error().message();
    *out_ok = false;
    return;
  }

  for (const auto& i : report->issues) {
    IssueRow r;
    r.rule_id = i.rule_id;
    r.severity = to_string(i.severity);
    r.entity_type = i.entity_type;
    r.entity_id = i.entity_id;
    r.message = i.message;
    r.suggestion = i.suggestion;
    out_issues->push_back(std::move(r));
  }
  *out_error = report->error_count;
  *out_warning = report->warning_count;
  *out_info = report->info_count;
  *out_rules_run = report->rules_run;
  *out_rules_total = report->rules_total;
  *out_ok = true;
}

ValidationPanel::~ValidationPanel() { stop_worker(); }

void ValidationPanel::stop_worker() {
  if (worker_.joinable()) {
    worker_.join();
  }
  running_ = false;
}

void ValidationPanel::on_database_closed() {
  stop_worker();  // 换库前停线程：旧 session 的查询不能再跑
  issues_.clear();
  error_count_ = 0;
  warning_count_ = 0;
  info_count_ = 0;
  rules_run_ = 0;
  rules_total_ = 0;
  ran_ = false;
  pending_.clear();
  pending_ready_ = false;
}

void ValidationPanel::start_run(DbSession& session) {
  if (running_) return;
  stop_worker();
  ran_ = false;
  pending_.clear();
  pending_error_ = pending_warning_ = pending_info_ = 0;
  pending_rules_run_ = pending_rules_total_ = 0;
  pending_status_.clear();
  pending_ok_ = false;
  DbSession* s = &session;
  running_ = true;
  worker_ = std::thread([this, s] {
    // 第六轮 高-2：异常不得逃出线程入口，否则 std::terminate
    try {
      run_validation_worker(*s, &pending_, &pending_error_, &pending_warning_,
                            &pending_info_, &pending_rules_run_,
                            &pending_rules_total_, &pending_status_,
                            &pending_ok_);
    } catch (const std::exception& e) {
      pending_.clear();
      pending_status_ = std::string("校验异常: ") + e.what();
      pending_ok_ = false;
    } catch (...) {
      pending_.clear();
      pending_status_ = "校验异常（未知）";
      pending_ok_ = false;
    }
    pending_ready_ = true;
    running_ = false;  // 发布点（release），UI 线程此帧起可采纳
  });
}

void ValidationPanel::poll() {
  if (running_ || !pending_ready_) return;
  stop_worker();
  issues_ = std::move(pending_);
  pending_.clear();
  error_count_ = pending_error_;
  warning_count_ = pending_warning_;
  info_count_ = pending_info_;
  rules_run_ = pending_rules_run_;
  rules_total_ = pending_rules_total_;
  ran_ = pending_ok_;
  if (pending_ok_) {
    state_.set_status("校验完成: error " + std::to_string(error_count_) +
                      " · warning " + std::to_string(warning_count_));
  } else {
    state_.set_status(pending_status_, true);
  }
  pending_ready_ = false;
}

}  // namespace genealogy::gui
