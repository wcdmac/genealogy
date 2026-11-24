#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/migration_service.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

MigrationService::MigrationService(Database& db) : db_(&db), repo_(db) {}

namespace {
// 空串可选地点 ID 折叠为未设置，否则 bind_opt_text 绑定 '' 触发 FK 失败（500）。
Migration normalize(const Migration& in) {
  Migration m = in;
  if (m.from_place_id && m.from_place_id->empty()) m.from_place_id.reset();
  if (m.to_place_id && m.to_place_id->empty()) m.to_place_id.reset();
  return m;
}
Error validate(const Migration& m) {
  if (m.subject_id.empty()) return invalid_argument("迁徙主体必填");
  if (!m.to_place_id) return invalid_argument("迁徙目的地必填");
  return {};
}
}  // namespace

Result<Migration> MigrationService::create_migration(const Migration& draft) {
  Migration normalized = normalize(draft);
  if (auto err = validate(normalized); err) return err;
  // 外键（subject/place 存在性）由 DDL 约束兜底；失败时仓储带回
  // sqlite 原始信息，够定位是哪个 ID 不存在。
  return repo_.insert(normalized);
}

Result<Migration> MigrationService::get_migration(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Migration>> MigrationService::list_migrations(
    const MigrationFilter& filter) {
  return repo_.list(filter);
}

Result<Migration> MigrationService::update_migration(
    const Migration& draft) {
  Migration normalized = normalize(draft);
  if (normalized.id.empty()) return invalid_argument("id 必填");
  if (auto err = validate(normalized); err) return err;
  return repo_.update(normalized);
}

Result<void> MigrationService::remove_migration(const std::string& id) {
  // 预检与删除必须在同一写事务内，否则并发插入引用会留下孤儿（TOCTOU）。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
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
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
