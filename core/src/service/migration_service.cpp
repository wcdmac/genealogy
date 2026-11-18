#include <genealogy/service/migration_service.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

MigrationService::MigrationService(Database& db) : db_(&db), repo_(db) {}

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
  // ROADMAP M7.2：删除被引事实时 409——迁徙被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  auto cn = citations.count_for_subject(CitationSubjectType::Migration, id);
  if (!cn) return cn.error();
  const int n = *cn;
  if (n > 0) {
    return conflict("迁徙被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  return repo_.remove(id);
}

}  // namespace genealogy
