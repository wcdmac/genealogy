#include <genealogy/service/migration_service.hpp>

namespace genealogy {

MigrationService::MigrationService(Database& db) : repo_(db) {}

Result<Migration> MigrationService::create_migration(const Migration& draft) {
  if (draft.subject_id.empty()) return invalid_argument("迁徙主体必填");
  if (!draft.to_place_id || draft.to_place_id->empty()) {
    return invalid_argument("迁徙目的地必填");
  }
  // 外键（subject/place 存在性）由 DDL 约束兜底；失败时仓储带回
  // sqlite 原始信息，够定位是哪个 ID 不存在。
  return repo_.insert(draft);
}

Result<Migration> MigrationService::get_migration(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Migration>> MigrationService::list_migrations(
    const MigrationFilter& filter) {
  return repo_.list(filter);
}

Result<Migration> MigrationService::update_migration(
    const Migration& migration) {
  if (migration.id.empty()) return invalid_argument("id 必填");
  if (migration.subject_id.empty()) return invalid_argument("迁徙主体必填");
  return repo_.update(migration);
}

Result<void> MigrationService::remove_migration(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
