#include <genealogy/lineage/generation.hpp>

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <unordered_map>

namespace genealogy::lineage {

namespace {

// Kahn 式拓扑：从始祖（图中无父母节点者）向下推世代。
// 关系环上的节点永不入队，自然被排除（不入表 = 疑环，交校验引擎）。
std::unordered_map<std::string, int> generations_topo(const PersonGraph& g) {
  std::unordered_map<std::string, int> gen;
  std::unordered_map<std::string, int> pending;  // 未处理的在图父母数
  std::deque<std::string> queue;

  for (const auto& [id, node] : g.nodes()) {
    (void)node;
    int parents = 0;
    if (g.father_of(id) != nullptr) ++parents;
    // 与 children_of 的去重口径一致（graph.cpp 对同一父母两边挂入去重）：
    // father_id==mother_id 的脏数据若按 2 计入度，孩子会永久卡在
    // pending 里被排除出世代表（2026-11 静态审查 M3）。
    if (g.mother_of(id) != nullptr && g.mother_of(id) != g.father_of(id)) {
      ++parents;
    }
    pending[id] = parents;
    if (parents == 0) {
      gen[id] = 1;  // 始祖
      queue.push_back(id);
    }
  }

  while (!queue.empty()) {
    const std::string id = queue.front();
    queue.pop_front();
    const int base = gen[id];
    for (const Person* child : g.children_of(id)) {
      auto& known = gen[child->id];
      known = std::max(known, base + 1);
      auto& rest = pending[child->id];
      if (--rest == 0) queue.push_back(child->id);
    }
  }
  // 入队即世代已定；环上节点 pending 永不归零，不出现在 queue，
  // 但其 gen 可能被部分写入——仅保留实际处理过的节点。
  std::unordered_map<std::string, int> out;
  for (const auto& [id, value] : gen) {
    if (pending[id] == 0) out[id] = value;
  }
  return out;
}

}  // namespace

std::unordered_map<std::string, int> compute_generations(const PersonGraph& g) {
  return generations_topo(g);
}

Result<int> generation_of(const PersonGraph& g, const std::string& id) {
  if (!g.contains(id)) return not_found("人物不存在: " + id);
  const auto gens = compute_generations(g);
  const auto it = gens.find(id);
  if (it == gens.end()) {
    return Error{ErrorCode::Internal,
                 "无法从始祖推算世代（疑关系环或断代）: " + id};
  }
  return it->second;
}

Result<int> generation_no_gap(const PersonGraph& g, const std::string& ego,
                              const std::string& alter) {
  if (!g.contains(ego)) return not_found("人物不存在: " + ego);
  if (!g.contains(alter)) return not_found("人物不存在: " + alter);
  const auto gens = compute_generations(g);
  const auto e = gens.find(ego);
  const auto a = gens.find(alter);
  if (e == gens.end() || a == gens.end()) {
    return Error{ErrorCode::Internal, "无法从始祖推算世代（疑关系环或断代）"};
  }
  // 跨连通分量拒绝比较（第五轮 P2-7）：两棵互不相干的树各自从第 1 世
  // 起算，直接相减会把陌生人读成「同辈」。沿父母/子女边查可达性。
  {
    std::unordered_set<std::string> visited{ego};
    std::vector<std::string> frontier{ego};
    const int kMaxHops = 100000;
    for (int hops = 0; !frontier.empty() && hops < kMaxHops; ++hops) {
      const std::string cur = frontier.back();
      frontier.pop_back();
      if (cur == alter) break;
      if (const auto* f = g.father_of(cur)) {
        if (visited.insert(f->id).second) frontier.push_back(f->id);
      }
      if (const auto* m = g.mother_of(cur)) {
        if (visited.insert(m->id).second) frontier.push_back(m->id);
      }
      // 配偶边也算连通（第五轮 B9 修正）：姻亲辈分差本就依赖配偶步
      // （妻→公公），只走父母/子女边会把合法姻亲误判成跨支。
      for (const auto* sp : g.spouses_of(cur)) {
        if (visited.insert(sp->id).second) frontier.push_back(sp->id);
      }
      for (const auto* next : g.children_of(cur)) {
        if (visited.insert(next->id).second) frontier.push_back(next->id);
      }
    }
    if (!visited.count(alter)) {
      return Error{ErrorCode::InvalidArgument,
                   "两人分属不同宗族分支（谱面无连通路径），世代序号不可比"};
    }
  }
  return a->second - e->second;
}

}  // namespace genealogy::lineage
