#include <genealogy/service/generation_poem_service.hpp>

#include <string>
#include <unordered_set>
#include <vector>

#include <genealogy/lineage/generation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/book.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy {

GenerationPoemService::GenerationPoemService(Database& db)
    : poems_(db),
      books_(db),
      branches_(db),
      persons_(db),
      unions_(db) {}

Result<std::vector<GenerationPoemEntry>> GenerationPoemService::set_poem(
    PoemOwner owner, const std::string& owner_id, const std::string& text) {
  // 单谱单库：谱级派语固定挂 book（与 0007 迁移产物一致）。历史调用方
  // 传任意 lineage id 一律归一——否则同一份派语按 owner_id 分裂成多份，
  // get/check 用的又是另一份（第四轮 P2）。
  const std::string oid =
      owner == PoemOwner::Lineage ? std::string("book") : owner_id;
  // 归属必须存在——派语挂在幽灵主体上，后续校验全部跑偏。
  // 第六轮 高-1：原实现丢弃 books_.get() 结果且缩进损坏，Lineage 归属
  // 完全没有校验。单谱单库下 book 行恒在，但异常迁移/外部改库时仍应
  // 给出明确错误，而不是把派语写到不存在的归属上。
  if (owner == PoemOwner::Lineage) {
    auto b = books_.get();
    if (!b) return b.error();
  } else {
    auto b = branches_.get(oid);
    if (!b) return not_found("支派不存在: " + oid);
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
    e.owner_id = oid;
    e.seq = static_cast<int>(i) + 1;
    e.character = chars[i];
    entries.push_back(std::move(e));
  }

  if (auto r = poems_.replace_all(owner, oid, entries); !r) {
    return r.error();
  }

  // 原文留档：结构化条目是权威，原文给谱师校对、也给重新生成用
  if (owner == PoemOwner::Lineage) {
    auto b = books_.get();
    if (b) {
      Book updated = *b;
      updated.generation_poem = text;
      // 第五轮测试恢复暴露：原实现误传 *b（未修改的旧对象），
      // 谱面原文 generation_poem 从未真正回写。
      if (auto r = books_.update(updated); !r) return r.error();
    }
  }
  return entries;
}

Result<std::vector<GenerationPoemEntry>> GenerationPoemService::get_poem(
    PoemOwner owner, const std::string& owner_id) {
  // 与 set_poem 归一口径一致（第四轮 P2）
  return poems_.list(owner,
                     owner == PoemOwner::Lineage ? std::string("book")
                                                 : owner_id);
}

Result<std::vector<lineage::PoemConflict>>
GenerationPoemService::check_consistency(PoemOwner owner,
                                         const std::string& owner_id,
                                         int poem_start) {
  // 与 set_poem 归一口径一致（第四轮 P2）
  const std::string oid =
      owner == PoemOwner::Lineage ? std::string("book") : owner_id;
  const auto entries = poems_.list(owner, oid);
  if (!entries) return entries.error();
  if (entries->empty()) {
    return invalid_argument("该归属尚未录入派语，无从校验");
  }
  std::vector<std::string> chars;
  chars.reserve(entries->size());
  for (const auto& e : *entries) chars.push_back(e.character);

  // 校验范围：lineage 归属 → 同宗人物，图与检查同范围。
  // branch 归属 → **图必须覆盖全宗族**（世代序号自始祖起算——只装支内
  // 人物会把支祖当根、世代塌陷：高阳李氏试修实测其凝「第 7 世」被算成
  // 「第 5 世」），检查范围才用支内成员过滤。
  // 曾经 lineage 模式不过滤、拉全库人物：别宗族灌进来的数据混进比对，
  // 误报淹真实信号（2026-10-23 端到端测试暴露：seed 灌入另一宗族的
  // 200 人全部参与 L 宗族派语校验，报 53 处假冲突）。
  PersonFilter pf;
  pf.limit = 200000;
  std::unordered_set<std::string> branch_members;
  if (owner == PoemOwner::Lineage) {
    // 与 set_poem 对齐（第六轮 高-1）：谱级派语的归属也要可验证
    auto b = books_.get();
    if (!b) return b.error();
  } else {
    auto b = branches_.get(oid);
    if (!b) return not_found("支派不存在: " + oid);
  }
  auto persons = persons_.list(pf);
  if (!persons) return persons.error();
  if (owner == PoemOwner::Branch) {
    for (const auto& p : *persons) {
      if (p.branch_id && *p.branch_id == oid) {
        branch_members.insert(p.id);
      }
    }
    if (branch_members.empty()) {
      return invalid_argument("该支派下没有标记归属的人物，无从校验");
    }
  }
  UnionFilter uf;
  uf.limit = 200000;
  auto unions = unions_.list(uf);
  if (!unions) return unions.error();

  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(std::move(*persons), std::move(*unions));
  const auto generations = lineage::compute_generations(graph);
  return lineage::check_poem_consistency(
      graph, generations, chars, poem_start,
      owner == PoemOwner::Branch ? &branch_members : nullptr);
}

}  // namespace genealogy
