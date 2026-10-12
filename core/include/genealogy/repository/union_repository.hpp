#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct UnionFilter {
  std::optional<std::string> person_id;  // 某人参与的全部结合
  int limit = 50;
  std::optional<std::string> cursor;
};

// 婚姻结合仓储：union 与 union_member 在同一事务内写入。
class UnionRepository {
 public:
  explicit UnionRepository(Database& db);

  Result<std::vector<Union>> list(const UnionFilter& filter) const;
  Result<Union> get(const std::string& id) const;
  Result<Union> insert(const Union& u);  // 含成员；成员需已存在（外键保证）
  Result<Union> update(const Union& u);  // 成员整体替换
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
