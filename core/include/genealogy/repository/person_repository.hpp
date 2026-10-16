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

 private:
  Database& db_;
};

}  // namespace genealogy
