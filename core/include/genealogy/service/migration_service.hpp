#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/migration.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/migration_repository.hpp>

namespace genealogy {

// 迁徙用例层。建档必填：主体（人/支派）+ 目的地。
// create_branch 选项由 REST 层处理（建支派是 branch 域的事，
// 服务层不跨域编排——迁徙只记自己的事实）。
class MigrationService {
 public:
  explicit MigrationService(Database& db);

  Result<Migration> create_migration(const Migration& draft);
  Result<Migration> get_migration(const std::string& id);
  Result<std::vector<Migration>> list_migrations(const MigrationFilter& filter);
  Result<Migration> update_migration(const Migration& migration);
  Result<void> remove_migration(const std::string& id);

 private:
  // 引用保护计数需要 CitationRepository（remove）
  Database* db_ = nullptr;
  MigrationRepository repo_;
};

}  // namespace genealogy
