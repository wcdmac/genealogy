// 校验规则引擎实现。
//
// 规则表（shared/rules/validation.json）在构建期嵌入；本文件负责
//   1. 解析规则表为 RuleDef；
//   2. 按 check 名分派到具体谓词；
//   3. 汇总成 ValidationReport。
//
// 谓词只依赖 PersonGraph 与（可选的）事件表 / 引用表，不碰数据库——
// 可单测、可被 REST 层与 CLI 复用。

#include <genealogy/validate/validation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <genealogy/validation/rules_data.hpp>

namespace genealogy {

namespace {

using genealogy::lineage::PersonGraph;

// ---------------------------------------------------------------- 规则表

struct RuleDef {
  std::string id;
  std::string name;
  IssueSeverity severity = IssueSeverity::Info;
  std::string entity;
  std::string check;
  std::string message;
  std::string suggestion;
  bool enabled = true;
  std::unordered_map<std::string, long long> params;
};

IssueSeverity severity_from(const std::string& s) {
  if (s == "error") return IssueSeverity::Error;
  if (s == "warning") return IssueSeverity::Warning;
  return IssueSeverity::Info;
}

// 规则表来自构建期嵌入的常量字符串，内容不可变——解析一次缓存复用。
// 此前每次 validate 调用都重新 parse 两遍（入口与规则分发处各一次），
// 纯浪费；function-local static 的初始化线程安全（C++11 起）。
const std::vector<RuleDef>& load_rules() {
  static const std::vector<RuleDef> kRules = [] {
    std::vector<RuleDef> out;
    const nlohmann::json j =
        nlohmann::json::parse(validation::rules_data::kValidationJson);
    for (const auto& r : j.at("rules")) {
      RuleDef d;
      d.id = r.value("id", std::string{});
      d.name = r.value("name", std::string{});
      d.severity = severity_from(r.value("severity", std::string("info")));
      d.entity = r.value("entity", std::string("person"));
      d.check = r.value("check", std::string{});
      d.message = r.value("message", std::string{});
      d.suggestion = r.value("suggestion", std::string{});
      d.enabled = r.value("enabled", true);
      if (const auto it = r.find("params"); it != r.end() && it->is_object()) {
        for (auto p = it->begin(); p != it->end(); ++p) {
          if (p->is_number_integer()) d.params[p.key()] = p->get<long long>();
        }
      }
      if (!d.id.empty() && !d.check.empty()) out.push_back(std::move(d));
    }
    return out;
  }();
  return kRules;
}

// 规则集元信息（ruleset 名称 / 版本号），与规则同源、同样只解析一次。
const nlohmann::json& rules_meta() {
  static const nlohmann::json kMeta = [] {
    return nlohmann::json::parse(validation::rules_data::kValidationJson);
  }();
  return kMeta;
}

long long param_of(const RuleDef& r, const std::string& key,
                   long long fallback) {
  const auto it = r.params.find(key);
  return it == r.params.end() ? fallback : it->second;
}

// 消息文案里的 {param} 占位符替换，让规则表能自己说清阈值
std::string render(const std::string& pattern, const RuleDef& r) {
  std::string out;
  std::size_t i = 0;
  while (i < pattern.size()) {
    if (pattern[i] == '{') {
      const std::size_t close = pattern.find('}', i);
      if (close != std::string::npos) {
        const std::string key = pattern.substr(i + 1, close - i - 1);
        const auto it = r.params.find(key);
        if (it != r.params.end()) {
          out += std::to_string(it->second);
          i = close + 1;
          continue;
        }
      }
    }
    out += pattern[i++];
  }
  return out;
}

// ---------------------------------------------------------------- 小工具

// 只认公历年份。农历原文换算不出公历时返回 nullopt——宁可少报，
// 不可拿不可靠的年份去判悖论。
std::optional<int> year_of(const GenealogyDate* d) {
  if (d == nullptr) return std::nullopt;
  return d->gregorian_year();
}

bool has_any_date(const GenealogyDate& d) {
  return d.gregorian.has_value() || d.lunar.has_value();
}

void add(std::vector<ValidationIssue>* out, const RuleDef& r,
         const std::string& entity_type, const std::string& entity_id,
         const std::string& detail = {}) {
  ValidationIssue issue;
  issue.rule_id = r.id;
  issue.severity = r.severity;
  issue.entity_type = entity_type;
  issue.entity_id = entity_id;
  issue.message = render(r.message, r);
  // detail：谓词级动态内容（如 R17 的 citation/来源清单），JSON 文案是
  // 静态模板覆盖不了的部分，追加在 message 之后
  if (!detail.empty()) issue.message += "（" + detail + "）";
  issue.suggestion = render(r.suggestion, r);
  out->push_back(std::move(issue));
}

// ---------------------------------------------------------------- 谓词

void check_birth_after_death(const RuleDef& r, const PersonGraph& g,
                             const std::vector<Event>*, const std::vector<Citation>*,
                             std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    if (!p->death_date) continue;
    const auto b = year_of(&p->birth_date);
    const auto d = year_of(&*p->death_date);
    if (!b || !d) continue;
    if (*b > *d) { add(out, r, "person", id); continue; }
    // 第五轮 低-2：同一年内出生晚于卒（如 1900-12 生 / 1900-06 卒）
    // 仅比年份会漏报。两者均为 Day 精度时用 ISO 串直接比较（yyyy-mm-dd
    // 字典序即时间序）；精度不足仍按年口径，避免误报。
    if (*b == *d && p->birth_date.precision == DatePrecision::Day &&
        p->death_date->precision == DatePrecision::Day) {
      const auto bs = p->birth_date.sort_key();
      const auto ds = p->death_date->sort_key();
      if (bs && ds && *bs > *ds) add(out, r, "person", id);
    }
  }
}

void check_parent_born_after_child(const RuleDef& r, const PersonGraph& g,
                                   const std::vector<Event>*, const std::vector<Citation>*,
                                   std::vector<ValidationIssue>* out) {
  const long long min_age = param_of(r, "min_parent_age_years", 12);
  for (const auto& [id, p] : g.nodes()) {
    const auto c = year_of(&p->birth_date);
    if (!c) continue;
    for (const Person* parent : {g.father_of(id), g.mother_of(id)}) {
      if (parent == nullptr) continue;
      const auto py = year_of(&parent->birth_date);
      if (!py) continue;
      if (static_cast<long long>(*c) - static_cast<long long>(*py) < min_age) {
        add(out, r, "person", id);
      }
    }
  }
}

void check_child_born_after_parent_death(const RuleDef& r, const PersonGraph& g,
                                         const std::vector<Event>*, const std::vector<Citation>*,
                                         std::vector<ValidationIssue>* out) {
  const long long max_mother = param_of(r, "max_mother_age", 55);
  const long long max_father = param_of(r, "max_father_age", 80);
  for (const auto& [id, p] : g.nodes()) {
    const auto c = year_of(&p->birth_date);
    if (!c) continue;
    const Person* father = g.father_of(id);
    const Person* mother = g.mother_of(id);
    if (father && father->death_date) {
      if (const auto dy = year_of(&*father->death_date)) {
        if (static_cast<long long>(*c) - static_cast<long long>(*dy) >
            max_father) {
          add(out, r, "person", id);
        }
      }
    }
    if (mother && mother->death_date) {
      if (const auto dy = year_of(&*mother->death_date)) {
        if (static_cast<long long>(*c) - static_cast<long long>(*dy) >
            max_mother) {
          add(out, r, "person", id);
        }
      }
    }
  }
}

void check_self_parent(const RuleDef& r, const PersonGraph& g,
                       const std::vector<Event>*, const std::vector<Citation>*,
                       std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    if ((p->father_id && *p->father_id == id) ||
        (p->mother_id && *p->mother_id == id)) {
      add(out, r, "person", id);
    }
  }
}

void check_duplicate_person(const RuleDef& r, const PersonGraph& g,
                            const std::vector<Event>*, const std::vector<Citation>*,
                            std::vector<ValidationIssue>* out) {
  const long long tol = param_of(r, "year_tolerance", 0);
  // 同名的人生年比对：按 (姓,名) 分桶，桶内两两比对——此前是全库
  // 两两比对，O(n²) 在 20 万级 ≈ 2×10¹⁰ 次，校验直接不可用；分桶后退
  // 化为同名簇内的小规模比对（同名本就罕见）。输出经入口处的
  // (rule_id, entity_id) 终排序，遍历顺序不影响确定性。
  std::unordered_set<std::string> reported;
  std::unordered_map<std::string, std::vector<const Person*>> buckets;
  buckets.reserve(g.nodes().size());
  for (const auto& [id, p] : g.nodes()) {
    (void)id;
    buckets[p->family_name + '\x1F' + p->given_name].push_back(p);
  }
  for (auto& [key, group] : buckets) {
    (void)key;
    if (group.size() < 2) continue;
    for (std::size_t i = 0; i < group.size(); ++i) {
      for (std::size_t k = i + 1; k < group.size(); ++k) {
        const Person* a = group[i];
        const Person* b = group[k];
        const auto ay = year_of(&a->birth_date);
        const auto by = year_of(&b->birth_date);
        if (!ay || !by) continue;
        if (std::llabs(static_cast<long long>(*ay) -
                       static_cast<long long>(*by)) > tol) {
          continue;
        }
        // 报 id 较小的一方，另一方由谱师按同名+生年自行比对；
        // 同一人只报一次，避免三个同名互相炸出三条
        const std::string& reported_id = (a->id < b->id) ? a->id : b->id;
        if (reported.insert(reported_id).second) {
          add(out, r, "person", reported_id);
        }
      }
    }
  }
}

void check_spouse_same_gender(const RuleDef& r, const PersonGraph& g,
                              const std::vector<Event>*, const std::vector<Citation>*,
                              std::vector<ValidationIssue>* out) {
  const std::vector<Union>* unions = g.unions();
  if (unions == nullptr) return;
  for (const auto& u : *unions) {
    bool male = false;
    bool female = false;
    for (const auto& m : u.members) {
      const Person* p = g.find(m.person_id);
      if (p == nullptr) continue;
      if (p->gender == Gender::Male) male = true;
      if (p->gender == Gender::Female) female = true;
    }
    // 两人性别都已知且相同才提示；一边未知不猜
    if ((male && !female) || (female && !male)) {
      if (u.members.size() >= 2) add(out, r, "union", u.id);
    }
  }
}

void check_parent_link_inconsistent(const RuleDef& r, const PersonGraph& g,
                                    const std::vector<Event>*, const std::vector<Citation>*,
                                    std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    if (!p->father_id && !p->mother_id) continue;
    if (p->father_id && !g.contains(*p->father_id)) {
      add(out, r, "person", id);
      continue;
    }
    if (p->mother_id && !g.contains(*p->mother_id)) {
      add(out, r, "person", id);
      continue;
    }
    // 父母都在图里时，必须存在同时包含二人的结合
    const std::string f = p->father_id.value_or(std::string{});
    const std::string m = p->mother_id.value_or(std::string{});
    // 仅登记一方（单亲、妾室未录母亲、续弦未录）是谱书常见形态，
    // 无从谈「同时包含二人」——R7 只约束双方都已登记的结合一致性。
    // （曾经有一方就查互链：母亲未录的人物全部误报 error，
    // 2026-10-23 端到端测试经 _CONC 妾室谱暴露。）
    if (f.empty() || m.empty()) continue;
    const auto spouses_f = g.spouses_of(f);
    const auto spouses_m = g.spouses_of(m);
    bool linked = false;
    for (const Person* s : spouses_f) {
      if (!m.empty() && s->id == m) linked = true;
    }
    for (const Person* s : spouses_m) {
      if (!f.empty() && s->id == f) linked = true;
    }
    if (!linked) add(out, r, "person", id);
  }
}

void check_generation_no_missing(const RuleDef& r, const PersonGraph& g,
                                 const std::vector<Event>*, const std::vector<Citation>*,
                                 std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    const bool has_parents =
        (p->father_id && g.contains(*p->father_id)) ||
        (p->mother_id && g.contains(*p->mother_id));
    if (has_parents && !p->generation_no) add(out, r, "person", id);
  }
}

void check_missing_gender(const RuleDef& r, const PersonGraph& g,
                          const std::vector<Event>*, const std::vector<Citation>*,
                          std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    if (p->gender == Gender::Unknown) add(out, r, "person", id);
  }
}

void check_missing_birth(const RuleDef& r, const PersonGraph& g,
                         const std::vector<Event>*, const std::vector<Citation>*,
                         std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    if (!has_any_date(p->birth_date)) add(out, r, "person", id);
  }
}

void check_ancestry_cycle(const RuleDef& r, const PersonGraph& g,
                          const std::vector<Event>*, const std::vector<Citation>*,
                          std::vector<ValidationIssue>* out) {
  // 三色 DFS（显式栈迭代）：0 未访问 / 1 在栈上 / 2 已完成。
  // 此前用 std::function 递归——数十万级的病态长父母链会递归爆栈
  // （tree/relationship 都是迭代或深度受控的，唯独这里裸奔）。
  // 报告语义与递归版一致：命中在栈节点时，当前栈路径全员入报。
  struct Frame {
    std::string id;
    std::size_t next = 0;
  };
  std::unordered_map<std::string, int> color;
  std::vector<Frame> stack;
  std::unordered_set<std::string> reported;
  // 父母链只取图内节点，先整体展开：迭代体内不再做 find/contains 判空
  std::unordered_map<std::string, std::vector<std::string>> parents_of;
  parents_of.reserve(g.nodes().size() * 2);
  for (const auto& [id, p] : g.nodes()) {
    (void)id;
    std::vector<std::string> parents;
    if (p->father_id && g.contains(*p->father_id)) parents.push_back(*p->father_id);
    if (p->mother_id && g.contains(*p->mother_id)) parents.push_back(*p->mother_id);
    parents_of.emplace(id, std::move(parents));
  }

  for (const auto& [id, p] : g.nodes()) {
    (void)p;
    if (color.find(id) != color.end()) continue;
    color[id] = 1;
    stack.push_back(Frame{id, 0});
    while (!stack.empty()) {
      const std::string top_id = stack.back().id;
      const auto& parents = parents_of[top_id];
      bool descended = false;
      while (stack.back().next < parents.size()) {
        // top 引用在 push 后会失效：next 计数经 stack.back() 存取，
        //父母表按 id 从预展开表取（引用稳定）
        const std::string q = parents[stack.back().next++];
        const auto it = color.find(q);
        if (it == color.end()) {
          color[q] = 1;
          stack.push_back(Frame{q, 0});
          descended = true;
          break;
        }
        if (it->second == 1) {
          // 回到栈上：环。报当前栈路径全员，避免同一条环被反复报
          for (const auto& s : stack) reported.insert(s.id);
        }
      }
      if (descended) continue;
      color[stack.back().id] = 2;
      stack.pop_back();
    }
  }
  for (const auto& id : reported) add(out, r, "person", id);
}

void check_date_unresolved(const RuleDef& r, const PersonGraph& g,
                           const std::vector<Event>*, const std::vector<Citation>*,
                           std::vector<ValidationIssue>* out) {
  for (const auto& [id, p] : g.nodes()) {
    if (p->birth_date.lunar && !p->birth_date.gregorian) {
      add(out, r, "person", id);
    } else if (p->death_date && p->death_date->lunar &&
               !p->death_date->gregorian) {
      add(out, r, "person", id);
    }
  }
}

void check_burial_before_death(const RuleDef& r, const PersonGraph& g,
                               const std::vector<Event>* events, const std::vector<Citation>* citations,
                               std::vector<ValidationIssue>* out) {
  if (events == nullptr) return;
  for (const auto& e : *events) {
    if (e.type != EventType::Burial) continue;
    if (e.subject_type != EventSubjectType::Person) continue;
    const Person* p = g.find(e.subject_id);
    if (p == nullptr || !p->death_date) continue;
    if (!e.date) continue;  // 葬期未登记时跳过（空 optional 解引用是 UB）
    const auto by = year_of(&*e.date);
    const auto dy = year_of(&*p->death_date);
    if (!by || !dy) continue;
    if (*by < *dy) add(out, r, "person", e.subject_id);
  }
}

void check_birth_order_gap(const RuleDef& r, const PersonGraph& g,
                           const std::vector<Event>*, const std::vector<Citation>*,
                           std::vector<ValidationIssue>* out) {
  // 同父或同母即算同胞；收集各自的 birth_order，看是否连续
  std::unordered_map<std::string, std::vector<int>> by_parent;
  for (const auto& [id, p] : g.nodes()) {
    if (!p->birth_order) continue;
    if (p->father_id && g.contains(*p->father_id)) {
      by_parent[*p->father_id].push_back(*p->birth_order);
    }
    if (p->mother_id && g.contains(*p->mother_id)) {
      by_parent[*p->mother_id].push_back(*p->birth_order);
    }
  }
  std::unordered_set<std::string> reported;
  for (auto& [parent, orders] : by_parent) {
    std::sort(orders.begin(), orders.end());
    orders.erase(std::unique(orders.begin(), orders.end()), orders.end());
    if (orders.size() < 2) continue;
    // 允许从 1 起，也允许从 0 起（行几从 0 记的谱也有）
    const int span = orders.back() - orders.front() + 1;
    if (span > static_cast<int>(orders.size())) {
      // 报 id 最小的那个孩子，方便谱师直接定位（迭代顺序不定，故按 id 取）
      const Person* first = nullptr;
      for (const auto& [id, p] : g.nodes()) {
        if ((p->father_id && *p->father_id == parent) ||
            (p->mother_id && *p->mother_id == parent)) {
          if (first == nullptr || p->id < first->id) first = p;
        }
      }
      if (first != nullptr) reported.insert(first->id);
    }
  }
  for (const auto& id : reported) add(out, r, "person", id);
}

// R16 无证据事实（M7.3）：主体级粒度——一个 person / event 即一个事实
// 单位，citation 挂接（含 locator 字段级精化）在主体维度计数。locator 是
// 自由文本、schema 无索引，字段级枚举需要虚构事实分类学且不可查询，
// 故字段级精化留作 v2；主体级与转录工作台的实际挂接方式完全对齐。
void check_missing_citation(const RuleDef& r, const PersonGraph& g,
                            const std::vector<Event>* events,
                            const std::vector<Citation>* citations,
                            std::vector<ValidationIssue>* out) {
  if (citations == nullptr) return;  // 与 events 的既有约定一致：无输入即跳过
  std::unordered_set<std::string> cited;
  cited.reserve(citations->size() * 2);
  for (const auto& c : *citations) {
    cited.insert(std::string(to_string(c.subject_type)) + ":" + c.subject_id);
  }
  for (const auto& [id, p] : g.nodes()) {
    if (cited.count(std::string("person:") + id) == 0) {
      add(out, r, "person", id);
    }
  }
  if (events != nullptr) {
    for (const auto& e : *events) {
      if (cited.count(std::string("event:") + e.id) == 0) {
        add(out, r, "event", e.id);
      }
    }
  }
}

// R17 引用冲突待裁定（M7.3）：citation 只存 source_id+locator+note，不存
// 事实取值，机器无法证明「内容互斥」；可执行的诚实判定是——同一主体、
// 同一非空 locator、两条 high 置信、来源不同 → 互斥嫌疑，提请人工对照。
// 裁定出口即 confidence 四档中的 disputed（citation.hpp 预留的入口）。
// 分桶结果必须先收集再按 key 排序：unordered_map 迭代不确定，而输出
// 确定性（规则序→实体序）是快照测试的既定不变量。
void check_conflicting_citations(const RuleDef& r, const PersonGraph&,
                                 const std::vector<Event>*,
                                 const std::vector<Citation>* citations,
                                 std::vector<ValidationIssue>* out) {
  if (citations == nullptr) return;
  struct Group {
    std::string sort_key;  // 主体类型 + id + locator，输出顺序的唯一来源
    std::string subject_type;
    std::string subject_id;
    std::string locator;
    std::vector<const Citation*> members;
  };
  std::unordered_map<std::string, Group> groups;
  for (const auto& c : *citations) {
    if (c.confidence != Confidence::High || !c.locator) continue;
    std::string loc;
    for (const char ch : *c.locator) {
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') loc.push_back(ch);
    }
    if (loc.empty()) continue;  // 主体级整档引用：跨来源多挂是正常互证
    const std::string type = to_string(c.subject_type);
    Group& grp = groups[type + ":" + c.subject_id + ":" + loc];
    grp.sort_key = type + ":" + c.subject_id + ":" + loc;
    grp.subject_type = type;
    grp.subject_id = c.subject_id;
    grp.locator = loc;
    grp.members.push_back(&c);
  }
  std::vector<const Group*> sorted;
  sorted.reserve(groups.size());
  for (const auto& kv : groups) sorted.push_back(&kv.second);
  std::sort(sorted.begin(), sorted.end(),
            [](const Group* a, const Group* b) { return a->sort_key < b->sort_key; });
  for (const Group* grp : sorted) {
    std::unordered_set<std::string> sources;
    for (const auto* c : grp->members) sources.insert(c->source_id);
    if (sources.size() < 2) continue;  // 同源重复挂接是数据卫生问题，非冲突
    std::string detail;
    for (const auto* c : grp->members) {
      if (!detail.empty()) detail += " / ";
      detail += "citation " + c->id + "（来源 " + c->source_id + "）";
    }
    add(out, r, grp->subject_type, grp->subject_id,
        "「" + grp->locator + "」：" + detail);
  }
}

struct CheckEntry {
  const char* name;
  void (*fn)(const RuleDef&, const PersonGraph&, const std::vector<Event>*, const std::vector<Citation>*,
             std::vector<ValidationIssue>*);
};

// 谓词注册表：规则表里的 check 值必须能在这里找到，
// 否则 validate() 直接报错——不允许 silently 跳过一条规则。
const std::array<CheckEntry, 16>& check_table() {
  static const std::array<CheckEntry, 16> kTable = {{
      {"birth_after_death", check_birth_after_death},
      {"parent_born_after_child", check_parent_born_after_child},
      {"child_born_after_parent_death", check_child_born_after_parent_death},
      {"self_parent", check_self_parent},
      {"duplicate_person", check_duplicate_person},
      {"spouse_same_gender", check_spouse_same_gender},
      {"parent_link_inconsistent", check_parent_link_inconsistent},
      {"generation_no_missing", check_generation_no_missing},
      {"missing_gender", check_missing_gender},
      {"missing_birth", check_missing_birth},
      {"ancestry_cycle", check_ancestry_cycle},
      {"date_unresolved", check_date_unresolved},
      {"burial_before_death", check_burial_before_death},
      {"birth_order_gap", check_birth_order_gap},
      {"missing_citation", check_missing_citation},
      {"conflicting_citations", check_conflicting_citations},
  }};
  return kTable;
}

}  // namespace

Result<ValidationReport> validate(const PersonGraph& graph,
                                  const std::vector<Event>* events, const std::vector<Citation>* citations,
                                  const ValidationOptions& opt) {
  const auto rules = load_rules();
  ValidationReport report;
  report.rules_total = static_cast<int>(rules.size());

  const nlohmann::json& j = rules_meta();
  report.ruleset = j.value("ruleset", std::string{});
  report.ruleset_version = std::to_string(j.value("version", 0));

  for (const auto& r : rules) {
    if (!r.enabled) continue;
    if (!opt.only_rule_ids.empty() &&
        std::find(opt.only_rule_ids.begin(), opt.only_rule_ids.end(), r.id) ==
            opt.only_rule_ids.end()) {
      continue;
    }
    const CheckEntry* entry = nullptr;
    for (const auto& e : check_table()) {
      if (r.check == e.name) {
        entry = &e;
        break;
      }
    }
    if (entry == nullptr) {
      return Error{ErrorCode::Internal,
                   "校验规则 " + r.id + " 的 check「" + r.check +
                       "」没有对应谓词实现——规则表与代码不同步"};
    }
    entry->fn(r, graph, events, citations, &report.issues);
    ++report.rules_run;
  }

  for (const auto& i : report.issues) {
    switch (i.severity) {
      case IssueSeverity::Error: ++report.error_count; break;
      case IssueSeverity::Warning: ++report.warning_count; break;
      case IssueSeverity::Info: ++report.info_count; break;
    }
  }
  // 输出顺序确定，便于快照测试：规则序 → 实体 id
  std::stable_sort(report.issues.begin(), report.issues.end(),
                   [](const ValidationIssue& a, const ValidationIssue& b) {
                     if (a.rule_id != b.rule_id) return a.rule_id < b.rule_id;
                     return a.entity_id < b.entity_id;
                   });
  return report;
}

}  // namespace genealogy
