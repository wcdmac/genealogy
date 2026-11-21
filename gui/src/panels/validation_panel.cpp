#include <genealogy/gui/panels/validation_panel.hpp>

#include <imgui.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/validate/validation.hpp>

namespace genealogy::gui {

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

void ValidationPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }

  // 采纳后台结果（worker 以 running_ 翻转为发布点）
  if (!running_ && pending_ready_) {
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

  if (ImGui::Button("运行校验")) {
    if (!running_) {
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
        run_validation_worker(*s, &pending_, &pending_error_,
                              &pending_warning_, &pending_info_,
                              &pending_rules_run_, &pending_rules_total_,
                              &pending_status_, &pending_ok_);
        pending_ready_ = true;
        running_ = false;  // 发布点（release），UI 线程此帧起可采纳
      });
    }
  }
  ImGui::SameLine();
  if (running_) {
    ImGui::TextDisabled("校验中…（后台线程跑，UI 不冻结）");
    return;
  }
  ImGui::Text("规则 %d/%d", rules_run_, rules_total_);
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "error %d", error_count_);
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.2F, 1.0F), "warning %d",
                     warning_count_);
  ImGui::SameLine();
  ImGui::TextDisabled("info %d", info_count_);

  ImGui::Separator();

  if (!ran_) {
    ImGui::TextDisabled("点「运行校验」跑一遍全库一致性规则。");
    return;
  }
  if (issues_.empty()) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1.0F), "未发现问题");
    return;
  }

  if (ImGui::BeginTable("issues", 5,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("等级", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("规则", ImGuiTableColumnFlags_WidthFixed, 55);
    ImGui::TableSetupColumn("实体", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("说明");
    ImGui::TableSetupColumn("定位", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (const auto& i : issues_) {
      ImGui::TableNextRow();
      const ImVec4 color = i.severity == "error"
                               ? ImVec4(1.0F, 0.4F, 0.4F, 1.0F)
                               : i.severity == "warning"
                                     ? ImVec4(1.0F, 0.85F, 0.2F, 1.0F)
                                     : ImVec4(0.6F, 0.6F, 0.6F, 1.0F);
      ImGui::TableNextColumn();
      ImGui::TextColored(color, "%s", i.severity.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(i.rule_id.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(i.entity_type.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(i.message.c_str());
      ImGui::TableNextColumn();
      // 点击定位：人物问题跳到人物卡（entity_type == "person" 才有意义）
      if (i.entity_type == "person" &&
          ImGui::SmallButton(("查看##" + i.entity_id).c_str())) {
        state_.selected_person_id = i.entity_id;
      }
    }
    ImGui::EndTable();
  }
}

}  // namespace genealogy::gui
