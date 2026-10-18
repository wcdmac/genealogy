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
  std::optional<std::string> branch_id;  // 该支派成员的迁徙（含支派自身）
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
