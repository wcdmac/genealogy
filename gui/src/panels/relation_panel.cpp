#include <genealogy/gui/panels/relation_panel.hpp>

#include <imgui.h>

#include <cstdio>
#include <string>

#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>

namespace genealogy::gui {

void RelationPanel::compute(DbSession& session) {
  computed_ = true;
  has_path_ = false;
  error_.clear();

  const std::string ego = state_.relation_ego.empty()
                              ? state_.selected_person_id
                              : state_.relation_ego;
  const std::string alter = state_.relation_alter;
  if (ego.empty() || alter.empty()) {
    error_ = "需要两个人：先在别处选中一人，再填另一个 id";
    return;
  }
  if (ego == alter) {
    error_ = "同一个人";
    return;
  }

  // 装载全库建图（与 CLI person relation 同路径：M1 规模全量装载）。
  // 图自持数据（build 按值移入），仓储结果随即可弃——这里必须用
  // std::move，把 Result 里的向量搬进图，否则又是一次全量拷贝。
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = session.persons().list_persons(pf);
  if (!persons) {
    error_ = "载入人物失败: " + persons.error().message();
    return;
  }
  UnionFilter uf;
  uf.limit = 200000;
  auto unions = session.unions().list_unions(uf);
  if (!unions) {
    error_ = "载入结合失败: " + unions.error().message();
    return;
  }
  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(std::move(*persons), std::move(*unions));

  const lineage::Relationship rel =
      lineage::find_relationship(graph, ego, alter);
  if (!rel.found) {
    error_ = "深度限制内未找到关系路径";
    return;
  }
  has_path_ = true;

  // 路径文字。relationship_key 的第二参是 ego 本人（用来定兄弟姊妹的
  // 长幼方向），必须判空——id 不在库里时 find 返回 nullptr，
  // 不解引用就是段错误（这类错只在数据不一致时出现，但出现了就该挡住）。
  const Person* ego_node = graph.find(ego);
  path_text_ = ego_node != nullptr
                   ? lineage::relationship_key(rel, *ego_node)
                   : std::string{};
  path_desc_.clear();
  for (const auto& s : rel.steps) {
    if (!path_desc_.empty()) path_desc_ += " → ";
    switch (s.kind) {
      case lineage::StepKind::Father: path_desc_ += "父"; break;
      case lineage::StepKind::Mother: path_desc_ += "母"; break;
      case lineage::StepKind::Son: path_desc_ += "子"; break;
      case lineage::StepKind::Daughter: path_desc_ += "女"; break;
      case lineage::StepKind::Husband: path_desc_ += "夫"; break;
      case lineage::StepKind::Wife: path_desc_ += "妻"; break;
      case lineage::StepKind::ElderBrother: path_desc_ += "兄"; break;
      case lineage::StepKind::YoungerBrother: path_desc_ += "弟"; break;
      case lineage::StepKind::ElderSister: path_desc_ += "姊"; break;
      case lineage::StepKind::YoungerSister: path_desc_ += "妹"; break;
    }
  }

  // 辈分差：沿路径的升降次数（上 -1 / 下 +1 / 配偶步 0）。
  // 注意与「世代序号差」不是一回事——姻亲常因父母未录而序号断链，
  // 辈分判断一律走这个值（设计文档 4.4.2）。
  generation_gap_ = rel.generation_gap();
  via_mother_ = rel.via_mother;
  via_marriage_ = rel.via_marriage;

  // 五服
  lineage::MourningQuery mq;
  mq.ego = ego;
  mq.alter = alter;
  if (auto md = lineage::compute_mourning_degree(graph, mq)) {
    mourning_garment_ = md->garment_name;
    mourning_period_ = md->period;
    mourning_months_ = md->period_months;
    mourning_rule_ = md->rule_id;
  } else {
    mourning_garment_ = "（无法计算）";
    mourning_period_.clear();
    mourning_months_ = 0;
    mourning_rule_.clear();
  }

  // 称谓
  lineage::AppellationQuery aq;
  aq.ego = ego;
  aq.alter = alter;
  if (auto app = lineage::compute_appellation(graph, aq)) {
    app_standard_ = app->standard;
    app_colloquial_ = app->colloquial;
    app_formal_ = app->formal;
    app_basis_ = app->basis;
    app_elder_ = app->elder;
    app_fallback_ = app->from_fallback;
    app_path_.clear();
    for (const auto& s : app->path) {
      if (!app_path_.empty()) app_path_ += ".";
      app_path_ += s;
    }
  } else {
    // 全部清空，避免残留上一次计算的称谓与当前"无法计算"自相矛盾
    app_standard_ = "（无法计算）";
    app_colloquial_.clear();
    app_formal_.clear();
    app_basis_ = app.error().message();
    app_path_.clear();
    app_elder_ = false;
    app_fallback_ = false;
  }
}

void RelationPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }

  ImGui::Text("甲方（ego）");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(260);
  // 输入框显示的是「显式值 or 全局选中」；编辑即变为显式值。
  const std::string ego_show = state_.relation_ego.empty()
                                   ? state_.selected_person_id
                                   : state_.relation_ego;
  char ego_buf[96] = {};
  std::snprintf(ego_buf, sizeof(ego_buf), "%s", ego_show.c_str());
  if (ImGui::InputText("##ego", ego_buf, sizeof(ego_buf))) {
    state_.relation_ego = ego_buf;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("（空 = 跟随全局选中）");

  ImGui::Text("乙方（alter）");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(260);
  char alter_buf[96] = {};
  std::snprintf(alter_buf, sizeof(alter_buf), "%s",
                state_.relation_alter.c_str());
  if (ImGui::InputText("##alter", alter_buf, sizeof(alter_buf))) {
    state_.relation_alter = alter_buf;
  }
  ImGui::SameLine();
  if (ImGui::Button("计算")) {
    compute(session);
  }

  ImGui::Separator();

  if (!computed_) {
    ImGui::TextDisabled("填好两个人后点「计算」。");
    return;
  }
  if (!has_path_) {
    ImGui::TextColored(ImVec4(1.0F, 0.4F, 0.4F, 1.0F), "%s", error_.c_str());
    return;
  }

  ImGui::SeparatorText("关系路径");
  ImGui::Text("字母序列: %s", path_text_.c_str());
  ImGui::Text("步骤: %s", path_desc_.c_str());
  ImGui::Text("辈分差: %+d（正=晚辈，负=尊长，0=同辈）", generation_gap_);
  if (via_mother_ || via_marriage_) {
    // 母系/姻亲标记：谱师据此判断该按血亲还是姻亲礼数，
    // 以及称谓有没有跨姓（妻之父母等）。
    ImGui::TextDisabled("%s%s", via_mother_ ? "经母系 " : "",
                        via_marriage_ ? "经婚姻边" : "");
  }

  ImGui::SeparatorText("五服（ming_qing）");
  ImGui::Text("丧服: %s", mourning_garment_.c_str());
  ImGui::Text("服期: %s（%d 月）", mourning_period_.c_str(), mourning_months_);
  ImGui::TextDisabled("规则: %s", mourning_rule_.c_str());

  ImGui::SeparatorText("称谓");
  ImGui::Text("标准: %s", app_standard_.c_str());
  ImGui::Text("口语: %s   书面: %s", app_colloquial_.c_str(),
              app_formal_.c_str());
  if (!app_path_.empty()) {
    ImGui::TextDisabled("规则路径: %s", app_path_.c_str());
  }
  ImGui::TextDisabled("依据: %s", app_basis_.c_str());
  if (app_fallback_) {
    ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.2F, 1.0F),
                       "组合式描述（规则表未收录，待谱学顾问审定）");
  }
}

}  // namespace genealogy::gui
