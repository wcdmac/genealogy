#pragma once

#include <string>
#include <vector>

#include <genealogy/lineage/graph.hpp>

namespace genealogy::lineage {

// 路径一步的类型（ego 视角：该步到达节点相对上一节点的关系）。
enum class StepKind {
  Father,
  Mother,
  Son,
  Daughter,
  Husband,
  Wife,
  ElderBrother,
  YoungerBrother,
  ElderSister,
  YoungerSister,
};

struct Step {
  StepKind kind;
  const Person* node = nullptr;  // 该步到达的人物（不持有所有权）
  bool seniority_known = false;  // 兄弟姊妹步的长幼是否可判定
};

// ego → alter 的关系路径及其语义解析（设计文档 4.4.2）。
struct Relationship {
  std::vector<Step> steps;      // 不含 ego 自身；空 = 同一人
  bool found = false;           // 是否在深度限制内找到路径
  int up = 0;                   // 上行步数（父母）
  int down = 0;                 // 下行步数（子女）
  bool via_mother = false;      // 经过母亲边（母系权重）
  bool via_marriage = false;    // 经过婚姻边（姻亲）
  bool alter_is_male = false;   // 末端节点性别为男（unknown 记 false）

  // 辈分差（设计文档 4.4.2「世代升降次数 → 世代差」）：沿关系路径
  // 上行一步记 -1、下行一步记 +1，正 = 晚辈、负 = 尊长，0 = 同辈。
  // 配偶步不升降，故夫妻为 0；姻亲亦由此得正确辈分（如 妻之公公 = -1）。
  int generation_gap() const noexcept { return down - up; }
  const Person* alter_node() const {
    return steps.empty() ? nullptr : steps.back().node;
  }
};

// 血亲世代距：至共同祖先的上/下行代数（仅计父母/子女步；
// 婚姻边之后的血亲段不计入本宗五服，交由姻亲规则处理）。
struct BloodDistance {
  int up = 0;
  int down = 0;
};

// BFS 最短路径（边权相同）。max_depth 默认 12，覆盖九族"族兄弟"级
// （9 步）路径。邻居枚举顺序固定（父→母→子女→兄弟姐妹→配偶），
// 等长路径的选取因此确定，算法输出可复现。
Relationship find_relationship(const PersonGraph& g, const std::string& ego,
                               const std::string& alter,
                               int max_depth = 12);

// 规则表键：如 "F.B+.S+"。末端为血亲且存在同代参照节点（路径中同代
// 主干节点，或传入的 ego）时附长幼标记；配偶步与无参照节点不带标记。
std::string relationship_key(const Relationship& rel, const Person& ego);
std::string relationship_key(const Relationship& rel);

// 组合式书面描述（规则表未收录时的兜底）：如 "父之兄之子"。
std::string relationship_description(const Relationship& rel);

// 血亲世代距（供五服兜底推断）。
BloodDistance blood_distance(const Relationship& rel);

// 长幼比较：a 是否早于 b 出生（a 为长）。依据出生日期 → 行几 →
// 世代并列；信息不足返回 nullopt（调用方按"长幼未知"处理）。
std::optional<bool> born_before(const Person& a, const Person& b);

}  // namespace genealogy::lineage
