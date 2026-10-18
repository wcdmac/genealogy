#include <genealogy/service/generation_poem_service.hpp>

#include <string>
#include <vector>

#include <genealogy/lineage/generation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy {

GenerationPoemService::GenerationPoemService(Database& db)
    : poems_(db),
      lineages_(db),
      branches_(db),
      persons_(db),
      unions_(db) {}

Result<std::vector<GenerationPoemEntry>> GenerationPoemService::set_poem(
    PoemOwner owner, const std::string& owner_id, const std::string& text) {
  // 归属必须存在——派语挂在幽灵宗族上，后续校验全部跑偏
  if (owner == PoemOwner::Lineage) {
    auto l = lineages_.get(owner_id);
    if (!l) return not_found("宗族不存在: " + owner_id);
  } else {
    auto b = branches_.get(owner_id);
    if (!b) return not_found("支派不存在: " + owner_id);
  }

  const std::vector<std::string> chars = lineage::parse_poem(text);
  if (chars.empty()) {
    return invalid_argument("派语联句里没有可用汉字（至少需要一个辈字）");
  }
  // 辈序从 1 起连续：中途断序会让「第几句」与「第几世」的换算错位
  std::vector<GenerationPoemEntry> entries;
  entries.reserve(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    GenerationPoemEntry e;
    e.owner_type = owner;
    e.owner_id = owner_id;
    e.seq = static_cast<int>(i) + 1;
    e.character = chars[i];
    entries.push_back(std::move(e));
  }

  if (auto r = poems_.replace_all(owner, owner_id, entries); !r) {
    return r.error();
  }

  // 原文留档：结构化条目是权威，原文给谱师校对、也给重新生成用
  if (owner == PoemOwner::Lineage) {
    auto l = lineages_.get(owner_id);
    if (l) {
      Lineage updated = *l;
      updated.generation_poem = text;
      if (auto r = lineages_.update(updated); !r) return r.error();
    }
  }
  return entries;
}

Result<std::vector<GenerationPoemEntry>> GenerationPoemService::get_poem(
    PoemOwner owner, const std::string& owner_id) {
  return poems_.list(owner, owner_id);
}

Result<std::vector<lineage::PoemConflict>>
GenerationPoemService::check_consistency(PoemOwner owner,
                                         const std::string& owner_id,
                                         int poem_start) {
  const auto entries = poems_.list(owner, owner_id);
  if (!entries) return entries.error();
  if (entries->empty()) {
    return invalid_argument("该归属尚未录入派语，无从校验");
  }
  std::vector<std::string> chars;
  chars.reserve(entries->size());
  for (const auto& e : *entries) chars.push_back(e.character);

  // 校验范围：该归属下的人物。lineage 归属 → 全库同宗人物；
  // branch 归属 → 该支派及其子支派的人物（支派树递归）。
  // 一期简化为全库（M1 规模），支派过滤留待分支树遍历 API。
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = persons_.list(pf);
  if (!persons) return persons.error();
  UnionFilter uf;
  uf.limit = 200000;
  auto unions = unions_.list(uf);
  if (!unions) return unions.error();

  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(std::move(*persons), std::move(*unions));
  const auto generations = lineage::compute_generations(graph);
  return lineage::check_poem_consistency(graph, generations, chars, poem_start);
}

}  // namespace genealogy
