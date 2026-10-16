#include <genealogy/lineage/appellation.hpp>

#include <string>

#include <genealogy/lineage/relationship.hpp>

#include "lineage/rules.hpp"

namespace genealogy::lineage {

namespace {

// 关系路径 → 字母序列。兄弟姊妹长幼不可判定时退化为 B/Z，
// 与 relationship_key 的口径一致（两处都从 step_letter 出）。
std::vector<std::string> path_letters(const Relationship& rel) {
  std::vector<std::string> out;
  out.reserve(rel.steps.size());
  for (const auto& s : rel.steps) {
    const bool sibling = s.kind == StepKind::ElderBrother ||
                         s.kind == StepKind::YoungerBrother ||
                         s.kind == StepKind::ElderSister ||
                         s.kind == StepKind::YoungerSister;
    if (sibling && !s.seniority_known) {
      const bool male = s.kind == StepKind::ElderBrother ||
                        s.kind == StepKind::YoungerBrother;
      out.push_back(male ? "B" : "Z");
      continue;
    }
    out.push_back(step_letter(s.kind));
  }
  return out;
}

}  // namespace

Result<Appellation> compute_appellation(const PersonGraph& g,
                                        const AppellationQuery& q) {
  const Person* ego = g.find(q.ego);
  if (ego == nullptr) return not_found("人物不存在: " + q.ego);
  const Person* alter = g.find(q.alter);
  if (alter == nullptr) return not_found("人物不存在: " + q.alter);

  if (q.ego == q.alter) {
    Appellation self;
    self.standard = "自己";
    self.colloquial = "自己";
    self.formal = "自己";
    self.basis = "同一人";
    return self;
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
  if (const rules::AppellationRule* rule =
          rules::match_appellation(key, ctx)) {
    Appellation app;
    app.standard = rule->standard;
    app.colloquial = rule->colloquial;
    app.formal = rule->formal;
    app.elder = rel.generation_gap() < 0;
    app.path = path_letters(rel);
    app.basis = rule->basis;
    app.from_fallback = false;
    return app;
  }

  // 兜底：组合式书面描述（规则表未收录，交谱学顾问审定后补录）。
  const std::string desc = relationship_description(rel);
  Appellation app;
  app.standard = desc;
  app.colloquial = desc;
  app.formal = desc;
  app.elder = rel.generation_gap() < 0;
  app.path = path_letters(rel);
  app.basis = "组合式描述（规则表未收录，待谱学顾问审定）";
  app.from_fallback = true;
  return app;
}

}  // namespace genealogy::lineage
