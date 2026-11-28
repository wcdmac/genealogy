#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/migration_service.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/person_repository.hpp>

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

// 主体存在性：migration.subject_id 在 DDL 上**没有**外键（0001 注释与
// 实际不符），缺校验时任意字符串都能落库成幽灵迁徙，列表按主体过滤
// 永远查不到（第四轮 P2-1）。与 event_service 同口径逐类型检查。
Error check_subject_exists(const Migration& m, Database& db) {
  switch (m.subject_type) {
    case MigrationSubjectType::Person: {
      PersonRepository repo(db);
      auto p = repo.get(m.subject_id);
      if (!p) return not_found("迁徙主体（人物）不存在: " + m.subject_id);
      return {};
    }
    case MigrationSubjectType::Branch: {
      BranchRepository repo(db);
      auto b = repo.get(m.subject_id);
      if (!b) return not_found("迁徙主体（支派）不存在: " + m.subject_id);
      return {};
    }
  }
  return invalid_argument("迁徙主体类型未知");
}
}  // namespace

Result<Migration> MigrationService::create_migration(const Migration& draft) {
  Migration normalized = normalize(draft);
  if (auto err = validate(normalized); err) return err;
  // 地点存在性由 DDL 外键兜底；主体存在性这里显式校验（见 check_subject_exists）。
  if (auto err = check_subject_exists(normalized, *db_); err) return err;
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
  if (auto err = check_subject_exists(normalized, *db_); err) return err;
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
