#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/migration.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct MigrationFilter {
  std::optional<MigrationSubjectType> subject_type;
  std::optional<std::string> subject_id;
  // 该支派的迁徙（含支派自身与全部成员），仓储内子查询原生解析，
  // 游标与单主体路径同一口径——此前 REST 层逐成员查询合并，分页会丢。
  std::optional<std::string> branch_id;
  int limit = 50;
  std::optional<std::string> cursor;
};

class MigrationRepository {
 public:
  explicit MigrationRepository(Database& db);

  Result<std::vector<Migration>> list(const MigrationFilter& filter) const;
  Result<Migration> get(const std::string& id) const;
  Result<Migration> insert(Migration migration);
  Result<Migration> update(const Migration& migration);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
