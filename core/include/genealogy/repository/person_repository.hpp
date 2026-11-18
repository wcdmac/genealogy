#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct PersonFilter {
  std::optional<std::string> branch_id;
  std::optional<std::string> lineage_id;    // 按宗族过滤（REST /persons?lineage_id=）
  std::optional<int> generation_no;         // 精确世代
  std::optional<int> generation_from;       // 世代区间（含端）
  std::optional<int> generation_to;
  // 某人的子女（father_id 或 mother_id 命中即算，OR 语义）。
  // GUI 人物卡此前为此全量装载 20 万人物内存过滤——索引
  // idx_person_father/idx_person_mother 让它变成两条点查。
  std::optional<std::string> parent_any;
  int limit = 50;
  std::optional<std::string> cursor;  // 上页最后一条的 id（ULID 字典序游标）
};

// 人物仓储：行映射 + 查询。M0 每次调用现准备语句，M1 引入语句缓存。
class PersonRepository {
 public:
  explicit PersonRepository(Database& db);

  Result<std::vector<Person>> list(const PersonFilter& filter) const;
  Result<Person> get(const std::string& id) const;
  Result<std::vector<Person>> search(std::string_view keyword, int limit) const;

  // 插入：分配 id / rev / created_at / updated_at（入参这些字段被忽略）。
  Result<Person> insert(Person person);

  // 更新：rev 不匹配返回 Conflict（设计文档 5.2.1 并发条款）。
  Result<Person> update(const Person& person);

  Result<void> remove(const std::string& id);

  // 删除前的引用清查（2026-11 静态审查 H12）：人物仍被亲属/结合/支派
  // 引用时，FK 会让 DELETE 报错——先查出来给调用方干净的 409，
  // 而不是让约束错误落到 500。
  struct RelationRefs {
    int parent_child_links = 0;   // parent_child：为父或为子
    int union_memberships = 0;    // union_member
    int founded_branches = 0;     // branch.founder_person_id
    int child_refs = 0;           // 子女的 father_id/mother_id 列指向本人
                                  // （转录库亲子走列不走表——漏查曾致
                                  // 删父撞 FK 落 500，2026-11-19 实测）
  };
  Result<RelationRefs> count_relation_references(const std::string& id) const;
  // 清掉本人名号行（person_name.person_id 外键指向 person），随删除同事务
  Result<void> remove_names(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
