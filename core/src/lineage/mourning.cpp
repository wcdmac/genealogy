#include <genealogy/lineage/mourning.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <genealogy/lineage/relationship.hpp>

#include "lineage/rules.hpp"

namespace genealogy::lineage {

namespace {

MourningGrade grade_from_id(const std::string& id, bool* ok) {
  *ok = true;
  if (id == "zhancui") return MourningGrade::ZhanCui;
  if (id == "qicui_zhang") return MourningGrade::QiCuiZhang;
  if (id == "qicui_buzhang") return MourningGrade::QiCuiBuZhang;
  if (id == "qicui_wuyue") return MourningGrade::QiCuiWuYue;
  if (id == "qicui_sanyue") return MourningGrade::QiCuiSanYue;
  if (id == "dagong") return MourningGrade::DaGong;
  if (id == "xiaogong") return MourningGrade::XiaoGong;
  if (id == "sima") return MourningGrade::SiMa;
  if (id == "tanmian") return MourningGrade::TanMian;
  if (id == "wufu") return MourningGrade::WuFu;
  *ok = false;
  return MourningGrade::WuFu;
}

}  // namespace

std::vector<std::string> available_rulesets() { return {"ming_qing"}; }

Result<MourningDegree> compute_mourning_degree(const PersonGraph& g,
                                               const MourningQuery& q) {
  const Person* ego = g.find(q.ego);
  if (ego == nullptr) return not_found("人物不存在: " + q.ego);
  const Person* alter = g.find(q.alter);
  if (alter == nullptr) return not_found("人物不存在: " + q.alter);
  if (q.ego == q.alter) {
    return invalid_argument("不能计算自身与自身的服制");
  }
  if (q.ruleset != "ming_qing") {
    return invalid_argument("未知规则表: " + q.ruleset +
                            "（可用: ming_qing；ADR-6 规则外置）");
  }

  const Relationship rel = find_relationship(g, q.ego, q.alter);
  if (!rel.found) {
    return not_found("两人之间无可达的亲缘或婚姻路径");
  }

  rules::MatchContext ctx;
  if (ego->gender != Gender::Unknown) ctx.ego_gender = ego->gender;
  ctx.ego_married = g.is_married(q.ego);
  ctx.ego_is_heir = ego->is_heir;
  ctx.alter_married = g.is_married(q.alter);
  ctx.alter_is_heir = alter->is_heir;

  const std::string key = relationship_key(rel, *ego);
  const rules::MourningRule* rule = rules::match_mourning(key, ctx);
  if (rule != nullptr) {
    bool ok = false;
    const MourningGrade grade = grade_from_id(rule->grade, &ok);
    if (!ok) {
      return internal("规则表条目指向未知服等: " + rule->grade);
    }
    const auto& info = rules::mourning_grades().at(rule->grade);
    MourningDegree deg;
    deg.grade = grade;
    deg.garment_name = info.name;
    deg.period = info.period;
    deg.period_months = info.months;
    deg.basis = rule->basis;
    deg.ruleset = q.ruleset;
    deg.from_fallback = false;
    return deg;
  }

  // 兜底：规则表未收录的路径按血亲世代距推断（袒免/无服边界）。
  const BloodDistance bd = blood_distance(rel);
  MourningDegree deg;
  deg.ruleset = q.ruleset;
  deg.from_fallback = true;
  if (rel.via_marriage && bd.up == 0 && bd.down == 0) {
    deg.grade = MourningGrade::WuFu;
    deg.garment_name = "无服";
    deg.period = "无";
    deg.period_months = 0;
    deg.basis = "姻亲（规则表未收录，按无服）";
    return deg;
  }
  const int depth = std::max(bd.up, bd.down);
  if (depth <= 4) {
    deg.grade = MourningGrade::SiMa;
    deg.garment_name = "缌麻";
    deg.period = "三月";
    deg.period_months = 3;
    deg.basis = "血亲世代距 " + std::to_string(depth) +
                "（规则表未收录，兜底推断）";
  } else if (depth == 5) {
    deg.grade = MourningGrade::TanMian;
    deg.garment_name = "袒免";
    deg.period = "无服期";
    deg.period_months = 0;
    deg.basis = "同五世祖（规则表未收录，兜底推断）";
  } else {
    deg.grade = MourningGrade::WuFu;
    deg.garment_name = "无服";
    deg.period = "无";
    deg.period_months = 0;
    deg.basis = "六世祖以外（规则表未收录，兜底推断）";
  }
  return deg;
}

}  // namespace genealogy::lineage
