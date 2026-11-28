#include <genealogy/lineage/mourning.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <genealogy/lineage/relationship.hpp>

#include "lineage/rules.hpp"

namespace genealogy::lineage {

namespace {

struct GradeEntry {
  MourningGrade grade;
  const char* id;
};

// 唯一一份对照表：id ↔ 枚举都从它出，避免两处漂移
const GradeEntry kGrades[] = {
    {MourningGrade::ZhanCui, "zhancui"},
    {MourningGrade::QiCuiZhang, "qicui_zhang"},
    {MourningGrade::QiCuiBuZhang, "qicui_buzhang"},
    {MourningGrade::QiCuiWuYue, "qicui_wuyue"},
    {MourningGrade::QiCuiSanYue, "qicui_sanyue"},
    {MourningGrade::DaGong, "dagong"},
    {MourningGrade::XiaoGong, "xiaogong"},
    {MourningGrade::SiMa, "sima"},
    {MourningGrade::TanMian, "tanmian"},
    {MourningGrade::WuFu, "wufu"},
};

MourningGrade grade_from_id(const std::string& id, bool* ok) {
  for (const auto& e : kGrades) {
    if (id == e.id) {
      *ok = true;
      return e.grade;
    }
  }
  *ok = false;
  return MourningGrade::WuFu;
}

}  // namespace

const char* to_string(MourningGrade grade) {
  for (const auto& e : kGrades) {
    if (e.grade == grade) return e.id;
  }
  return "wufu";
}

std::optional<MourningGrade> mourning_grade_from_string(const std::string& s) {
  bool ok = false;
  const auto g = grade_from_id(s, &ok);
  return ok ? std::optional(g) : std::nullopt;
}

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
    // find() 而非 .at()：grade_from_id 走硬编码枚举表、服等表来自
    // JSON——双源可漂移，漂移时 .at() 会在请求线程抛 out_of_range
    // （2026-11 静态审查 H14）
    const auto& grades = rules::mourning_grades();
    const auto grade_it = grades.find(rule->grade);
    if (grade_it == grades.end()) {
      return internal("规则表条目缺少服等定义: " + rule->grade);
    }
    const auto& info = grade_it->second;
    MourningDegree deg;
    deg.grade = grade;
    deg.garment_name = info.name;
    deg.period = info.period;
    deg.period_months = info.months;
    deg.rule_id = rule->path;
    deg.basis = rule->basis;
    deg.ruleset = q.ruleset;
    deg.from_fallback = false;
    return deg;
  }

  // 兜底：规则表未收录的路径按血亲世代距推断（袒免/无服边界）。
  const BloodDistance bd = blood_distance(rel);
  MourningDegree deg;
  deg.rule_id = "fallback";
  deg.ruleset = q.ruleset;
  deg.from_fallback = true;
  // 姻亲兜底（第五轮 P2-6）：凡经婚姻步且规则表未收录者一律无服。
  // 此前血亲段继续走 max(up,down) 推缌麻——亲家（S.W.F 等）会被推出
  // 毫无依据的缌麻；岳父母等有明确条目者走不到兜底，不受影响。
  if (rel.via_marriage) {
    deg.grade = MourningGrade::WuFu;
    deg.garment_name = "无服";
    deg.period = "无";
    deg.period_months = 0;
    deg.basis = "姻亲（规则表未收录，按无服）";
    return deg;
  }
  // 血亲兜底（第五轮 P2-6）：保留 max(up,down) 梯度——族兄弟快照
  // （F.F.F.B.S.S，max=3→缌麻）与传世服叙一致，sum 梯度反而错等；
  // 仅修正 depth==0 的兄弟姊妹档（旧算法给缌麻，应为期年）。兜底
  // 本就是近似，权威以规则表为准。
  const int depth = std::max(bd.up, bd.down);
  if (depth == 0) {
    deg.grade = MourningGrade::QiCuiBuZhang;
    deg.garment_name = "齐衰不杖期";
    deg.period = "一年";
    deg.period_months = 12;
    deg.basis = "同父母平辈（规则表未收录，兜底推断）";
    return deg;
  }
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
