#include <genealogy/lineage/graph.hpp>

#include <algorithm>

namespace genealogy::lineage {

namespace {

// 人物排序键：行几 → 出生日期 → id。行几与日期均缺时退到 id，保证确定性。
struct PersonOrderKey {
  int birth_order_rank;      // 0 = 有行次，1 = 无行次
  int birth_order;
  int date_rank;             // 0 = 有日期，1 = 无日期
  std::string date_key;
  std::string id;
};

PersonOrderKey order_key(const Person* p) {
  PersonOrderKey k;
  k.birth_order_rank = p->birth_order.has_value() ? 0 : 1;
  k.birth_order = p->birth_order.value_or(0);
  const auto sk = p->birth_date.sort_key();
  k.date_rank = sk.has_value() ? 0 : 1;
  k.date_key = sk.value_or(std::string());
  k.id = p->id;
  return k;
}

bool order_less(const Person* a, const Person* b) {
  const auto ka = order_key(a);
  const auto kb = order_key(b);
  if (ka.birth_order_rank != kb.birth_order_rank) {
    return ka.birth_order_rank < kb.birth_order_rank;
  }
  if (ka.birth_order != kb.birth_order) return ka.birth_order < kb.birth_order;
  if (ka.date_rank != kb.date_rank) return ka.date_rank < kb.date_rank;
  if (ka.date_key != kb.date_key) return ka.date_key < kb.date_key;
  return ka.id < kb.id;
}

}  // namespace

PersonGraph PersonGraph::build(const std::vector<Person>& persons,
                               const std::vector<Union>& unions) {
  PersonGraph g;
  for (const auto& p : persons) g.nodes_.emplace(p.id, &p);

  // 父母 → 子女索引
  for (const auto& p : persons) {
    if (p.father_id && g.nodes_.count(*p.father_id)) {
      g.children_[*p.father_id].push_back(&p);
    }
    if (p.mother_id && g.nodes_.count(*p.mother_id)) {
      g.children_[*p.mother_id].push_back(&p);
    }
  }
  for (auto& [id, kids] : g.children_) {
    std::sort(kids.begin(), kids.end(), order_less);
    // 同父同母两边都可能挂入，去重（保序）
    kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
  }

  // 兄弟姐妹：父母子女集合的并集去掉自己
  for (const auto& p : persons) {
    std::vector<const Person*> sibs;
    if (p.father_id) {
      const auto it = g.children_.find(*p.father_id);
      if (it != g.children_.end()) sibs = it->second;
    }
    if (p.mother_id) {
      const auto it = g.children_.find(*p.mother_id);
      if (it != g.children_.end()) {
        for (const auto* c : it->second) {
          if (std::find(sibs.begin(), sibs.end(), c) == sibs.end()) {
            sibs.push_back(c);
          }
        }
      }
    }
    std::erase_if(sibs, [&p](const Person* c) { return c->id == p.id; });
    std::sort(sibs.begin(), sibs.end(), order_less);
    g.siblings_[p.id] = std::move(sibs);
  }

  // 配偶：union 成员互挂（成员可能不在图中，跳过）
  for (const auto& u : unions) {
    for (const auto& m : u.members) {
      if (!g.nodes_.count(m.person_id)) continue;
      for (const auto& other : u.members) {
        if (other.person_id == m.person_id) continue;
        if (!g.nodes_.count(other.person_id)) continue;
        auto& list = g.spouses_[m.person_id];
        if (std::find(list.begin(), list.end(), g.nodes_.at(other.person_id)) ==
            list.end()) {
          list.push_back(g.nodes_.at(other.person_id));
        }
      }
    }
  }
  return g;
}

const Person* PersonGraph::find(const std::string& id) const {
  const auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : it->second;
}

bool PersonGraph::contains(const std::string& id) const {
  return nodes_.count(id) > 0;
}

const Person* PersonGraph::father_of(const std::string& id) const {
  const auto* p = find(id);
  if (p == nullptr || !p->father_id) return nullptr;
  return find(*p->father_id);
}

const Person* PersonGraph::mother_of(const std::string& id) const {
  const auto* p = find(id);
  if (p == nullptr || !p->mother_id) return nullptr;
  return find(*p->mother_id);
}

std::vector<const Person*> PersonGraph::children_of(
    const std::string& id) const {
  const auto it = children_.find(id);
  return it == children_.end() ? std::vector<const Person*>() : it->second;
}

std::vector<const Person*> PersonGraph::siblings_of(
    const std::string& id) const {
  const auto it = siblings_.find(id);
  return it == siblings_.end() ? std::vector<const Person*>() : it->second;
}

std::vector<const Person*> PersonGraph::spouses_of(
    const std::string& id) const {
  const auto it = spouses_.find(id);
  return it == spouses_.end() ? std::vector<const Person*>() : it->second;
}

bool PersonGraph::is_married(const std::string& id) const {
  const auto it = spouses_.find(id);
  return it != spouses_.end() && !it->second.empty();
}

}  // namespace genealogy::lineage
