#include <genealogy/lineage/relationship.hpp>

#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace genealogy::lineage {

namespace {

std::optional<bool> compare_sort_keys(const Person& a, const Person& b) {
  const auto ka = a.birth_date.sort_key();
  const auto kb = b.birth_date.sort_key();
  if (ka && kb && *ka != *kb) return *ka < *kb;
  // 日期精度不足/缺失：按行几（小为长）
  if (a.birth_order && b.birth_order && *a.birth_order != *b.birth_order) {
    return *a.birth_order < *b.birth_order;
  }
  return std::nullopt;
}

StepKind sibling_kind(const Person& from, const Person& to, bool* known) {
  *known = false;
  const auto elder = born_before(to, from);
  if (!elder) return to.gender == Gender::Male ? StepKind::YoungerBrother
                                               : StepKind::YoungerSister;
  *known = true;
  if (to.gender == Gender::Male) {
    return *elder ? StepKind::ElderBrother : StepKind::YoungerBrother;
  }
  return *elder ? StepKind::ElderSister : StepKind::YoungerSister;
}

// 路径中第 idx 步节点相对 ego 的世代（上行负、下行正）。
int gen_delta_at(const std::vector<Step>& steps, std::size_t idx) {
  int g = 0;
  for (std::size_t i = 0; i <= idx && i < steps.size(); ++i) {
    switch (steps[i].kind) {
      case StepKind::Father:
      case StepKind::Mother: --g; break;
      case StepKind::Son:
      case StepKind::Daughter: ++g; break;
      default: break;
    }
  }
  return g;
}

}  // namespace

std::optional<bool> born_before(const Person& a, const Person& b) {
  return compare_sort_keys(a, b);
}

Relationship find_relationship(const PersonGraph& g, const std::string& ego,
                               const std::string& alter, int max_depth) {
  Relationship rel;
  const Person* ego_node = g.find(ego);
  const Person* alter_node = g.find(alter);
  if (ego_node == nullptr || alter_node == nullptr) return rel;
  if (ego == alter) {
    rel.found = true;  // 同一人：steps 为空
    return rel;
  }

  struct State {
    const Person* node;
    int depth;
    std::vector<Step> path;
  };
  std::deque<State> queue;
  std::unordered_set<std::string> visited;

  auto push = [&](const Person* node, int depth, std::vector<Step> path) {
    if (depth > max_depth || visited.count(node->id)) return;
    visited.insert(node->id);
    queue.push_back(State{node, depth, std::move(path)});
  };

  push(ego_node, 0, {});

  while (!queue.empty()) {
    State cur = queue.front();
    queue.pop_front();
    if (cur.node->id == alter) {
      rel.found = true;
      rel.steps = std::move(cur.path);
      break;
    }
    if (cur.depth >= max_depth) continue;
    const int next_depth = cur.depth + 1;

    // 枚举顺序固定：父 → 母 → 子女 → 兄弟姐妹 → 配偶
    if (const Person* f = g.father_of(cur.node->id); f != nullptr) {
      auto path = cur.path;
      path.push_back(Step{StepKind::Father, f, false});
      push(f, next_depth, std::move(path));
    }
    if (const Person* m = g.mother_of(cur.node->id); m != nullptr) {
      auto path = cur.path;
      path.push_back(Step{StepKind::Mother, m, false});
      push(m, next_depth, std::move(path));
    }
    for (const Person* c : g.children_of(cur.node->id)) {
      auto path = cur.path;
      path.push_back(Step{c->gender == Gender::Male ? StepKind::Son
                                                    : StepKind::Daughter,
                          c, false});
      push(c, next_depth, std::move(path));
    }
    for (const Person* s : g.siblings_of(cur.node->id)) {
      bool known = false;
      const StepKind kind = sibling_kind(*cur.node, *s, &known);
      auto path = cur.path;
      path.push_back(Step{kind, s, known});
      push(s, next_depth, std::move(path));
    }
    // 配偶步按对方在结合中的角色判定（第五轮 P1-1）：妾此前被性别
    // 推导成 Wife，以「妻」的称谓与服等输出（以规则表背书的错误）。
    for (const PersonGraph::SpouseLink& sl : g.spouse_links_of(cur.node->id)) {
      const StepKind kind =
          sl.role == MemberRole::Concubine ? StepKind::Concubine
              : sl.role == MemberRole::Wife ? StepKind::Wife
                                            : StepKind::Husband;
      auto path = cur.path;
      path.push_back(Step{kind, sl.peer, false});
      push(sl.peer, next_depth, std::move(path));
    }
  }

  if (!rel.found) return rel;

  // 语义解析
  for (const auto& s : rel.steps) {
    switch (s.kind) {
      case StepKind::Father:
      case StepKind::Mother:
        ++rel.up;
        if (s.kind == StepKind::Mother) rel.via_mother = true;
        break;
      case StepKind::Son:
      case StepKind::Daughter:
        ++rel.down;
        break;
      case StepKind::Husband:
      case StepKind::Wife:
      case StepKind::Concubine:
        rel.via_marriage = true;
        break;
      default:
        break;
    }
  }
  rel.alter_is_male = alter_node->gender == Gender::Male;
  return rel;
}

// 注意定义在 genealogy::lineage 而非匿名 namespace：头文件里已声明它，
// 放匿名 namespace 里会让同一作用域出现两个候选，调用点报重载歧义。
const char* step_letter(StepKind kind) {
  switch (kind) {
    case StepKind::Father: return "F";
    case StepKind::Mother: return "M";
    case StepKind::Son: return "S";
    case StepKind::Daughter: return "D";
    case StepKind::Husband: return "H";
    case StepKind::Wife: return "W";
    case StepKind::Concubine: return "Q";
    case StepKind::ElderBrother: return "B+";
    case StepKind::YoungerBrother: return "B-";
    case StepKind::ElderSister: return "Z+";
    case StepKind::YoungerSister: return "Z-";
  }
  return "?";
}

namespace {

std::string relationship_key_impl(const Relationship& rel, const Person* ego) {
  if (rel.steps.empty()) return {};
  std::string key;
  const int terminal_gen = rel.generation_gap();
  for (std::size_t i = 0; i < rel.steps.size(); ++i) {
    const Step& s = rel.steps[i];
    if (i > 0) key += '.';
    const bool terminal = i + 1 == rel.steps.size();
    // 兄弟姊妹步：长幼可判定时附 +/−；出生信息不足时退化为无标记
    // 的 B/Z（匹配层展开 ± 变体，见 rules.cpp key_variants）。
    const bool sibling = s.kind == StepKind::ElderBrother ||
                         s.kind == StepKind::YoungerBrother ||
                         s.kind == StepKind::ElderSister ||
                         s.kind == StepKind::YoungerSister;
    if (sibling && !s.seniority_known) {
      const bool male = s.kind == StepKind::ElderBrother ||
                        s.kind == StepKind::YoungerBrother;
      key += male ? 'B' : 'Z';
      continue;
    }
    key += step_letter(s.kind);
    // 末端血亲（子/女）的长幼标记：参照 = 路径中同代最早节点；
    // 无路径内同代节点且末端与 ego 同代时，参照为 ego。
    if (terminal && (s.kind == StepKind::Son || s.kind == StepKind::Daughter)) {
      const Person* reference = nullptr;
      for (std::size_t j = 0; j + 1 < rel.steps.size(); ++j) {
        if (gen_delta_at(rel.steps, j) == terminal_gen) {
          reference = rel.steps[j].node;
          break;
        }
      }
      if (reference == nullptr && terminal_gen == 0 && ego != nullptr) {
        reference = ego;
      }
      if (reference != nullptr) {
        const auto elder = born_before(*s.node, *reference);
        if (elder) {
          key += *elder ? "+" : "-";
        }
      }
    }
  }
  return key;
}

}  // namespace

std::string relationship_key(const Relationship& rel, const Person& ego) {
  return relationship_key_impl(rel, &ego);
}

std::string relationship_key(const Relationship& rel) {
  return relationship_key_impl(rel, nullptr);
}

std::string relationship_description(const Relationship& rel) {
  if (rel.steps.empty()) return "自己";
  std::string out;
  for (std::size_t i = 0; i < rel.steps.size(); ++i) {
    if (i > 0) out += "之";
    switch (rel.steps[i].kind) {
      case StepKind::Father: out += "父"; break;
      case StepKind::Mother: out += "母"; break;
      case StepKind::Son: out += "子"; break;
      case StepKind::Daughter: out += "女"; break;
      case StepKind::Husband: out += "夫"; break;
      case StepKind::Wife: out += "妻"; break;
      case StepKind::Concubine: out += "妾"; break;
      case StepKind::ElderBrother: out += "兄"; break;
      case StepKind::YoungerBrother: out += "弟"; break;
      case StepKind::ElderSister: out += "姊"; break;
      case StepKind::YoungerSister: out += "妹"; break;
    }
  }
  return out;
}

BloodDistance blood_distance(const Relationship& rel) {
  BloodDistance bd;
  // 血亲段 = 首个婚姻步之前的父母/子女步 + 婚姻步之后的血亲步
  // （后者属姻亲引申，如 W.F 的 F 不计入本宗世代距）。
  for (const auto& s : rel.steps) {
    if (s.kind == StepKind::Husband || s.kind == StepKind::Wife) break;
    if (s.kind == StepKind::Father || s.kind == StepKind::Mother) ++bd.up;
    if (s.kind == StepKind::Son || s.kind == StepKind::Daughter) ++bd.down;
  }
  return bd;
}

}  // namespace genealogy::lineage
