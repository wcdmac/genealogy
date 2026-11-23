#include <genealogy/repository/database.hpp>

#include <sqlite3.h>

#include <algorithm>

#include <cstdint>
#include <memory>
#include <string>
#include <system_error>

#include <genealogy/repository/embedded_schema.hpp>
#include <genealogy/util/time.hpp>
#include <genealogy/util/sha256.hpp>

#include "sqlite_util.hpp"

namespace genealogy {
namespace {

struct SqliteCloser {
  void operator()(sqlite3* db) const noexcept {
    if (db != nullptr) sqlite3_close_v2(db);
  }
};

// exec 用 sqlite_util 的共享实现——本文件曾自留一份逐行相同的拷贝，
// 两处漂移只是时间问题。

// 返回 0=未应用，1=已应用（旧格式校验和，跳过比对），2=已应用且校验和匹配，
// -1=已应用但内容校验和不匹配（同版本不同结构的库）。
int migration_status(sqlite3* db, const char* name,
                     std::string_view sql) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db,
      "SELECT checksum FROM _schema_migrations WHERE name = ?1",
      -1, &st, nullptr);
  if (st == nullptr) return 0;
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 0; }
  const auto* raw = sqlite3_column_text(st, 0);
  const std::string stored = raw ? reinterpret_cast<const char*>(raw) : "";
  sqlite3_finalize(st);
  // 老库记录的是长度数字——视为历史格式，不做内容比对（避免老库升级受阻）。
  const bool old_format = !stored.empty() &&
      std::all_of(stored.begin(), stored.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
      });
  if (old_format) return 1;
  return stored == sha256_hex(sql) ? 2 : -1;
}

Result<void> record_migration(sqlite3* db, const char* name,
                              const std::string& sql) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(
      db, "INSERT INTO _schema_migrations(name, applied_at, checksum)"
          " VALUES (?1, ?2, ?3)",
      -1, &st, nullptr);
  if (st == nullptr) {
    return Error{ErrorCode::Internal, "准备迁移记录失败"};
  }
  const std::string now = utc_now_iso();
  // 校验和存真实 SHA-256；老库已有的纯数字长度记录由 migration_status
  // 识别为历史格式并跳过比对，保证平滑升级。
  const std::string checksum = sha256_hex(sql);
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, checksum.c_str(), -1, SQLITE_TRANSIENT);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  if (!ok) return Error{ErrorCode::Internal, "记录迁移失败"};
  return {};
}

}  // namespace

struct Database::Impl {
  std::unique_ptr<sqlite3, SqliteCloser> db;
  std::size_t applied = 0;
  std::recursive_mutex write_mu;  // M9.1 单写者：写事务全程持有
};

Result<Database> Database::open(const std::filesystem::path& path) {
  std::error_code ec;
  if (path.has_parent_path() && !path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    // ec 非空时交给 sqlite3_open_v2 报错，保持单一错误出口
  }

  sqlite3* raw = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                    SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.string().c_str(), &raw, flags, nullptr) !=
      SQLITE_OK) {
    if (raw != nullptr) sqlite3_close_v2(raw);
    return Error{ErrorCode::IoError, "无法打开数据库: " + path.string()};
  }

  auto impl = std::make_unique<Impl>();
  impl->db.reset(raw);

  // 设计文档 4.2：WAL + 外键。busy_timeout 5 秒：同进程无碍（连接
  // 串行化），但 server 与 GUI/CLI 同时开同一库文件时，写冲突应
  // 等待重试而不是立刻报 database is locked。
  sqlite3_busy_timeout(raw, 5000);
  if (auto r = repo_internal::exec(raw, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;");
      !r) {
    return r.error();
  }

  if (auto r = repo_internal::exec(raw,
                    "CREATE TABLE IF NOT EXISTS _schema_migrations ("
                    "  name       TEXT PRIMARY KEY,"
                    "  applied_at TEXT NOT NULL,"
                    "  checksum   TEXT NOT NULL"
                    ");");
      !r) {
    return r.error();
  }

  const auto* migrations = embedded_schema::migrations();
  const auto count = embedded_schema::migration_count();
  // 0007 用 DROP TABLE 重建 person/branch/document 以去 lineage_id；
  // 0008 用 DROP TABLE 重建 event/citation 以修复 lineage 孤儿主体。
  // foreign_keys=ON 时 DROP 会触发隐式 DELETE 的外键检查，非空存量库
  // 必然 FK 失败；SQLite 要求该 PRAGMA 在事务外切换，故此处按迁移
  // 粒度临时关闭，提交/回滚后立即恢复。
  constexpr std::string_view kRebuildMigrations[] = {
      "0007_single_book", "0008_orphan_subjects"};
  for (std::size_t i = 0; i < count; ++i) {
    const auto& m = migrations[i];
    const int status = migration_status(raw, m.name, m.sql);
    if (status == -1) {
      return Error{ErrorCode::Internal,
                   std::string("迁移校验和不匹配（同版本不同结构）: ") +
                       std::string(m.name)};
    }
    if (status != 0) {
      impl->applied = i + 1;
      continue;
    }
    const bool rebuild =
        std::find(std::begin(kRebuildMigrations), std::end(kRebuildMigrations),
                  m.name) != std::end(kRebuildMigrations);
    if (rebuild) {
      if (auto r = repo_internal::exec(raw, "PRAGMA foreign_keys=OFF;"); !r) return r.error();
    }
    if (auto r = repo_internal::exec(raw, "BEGIN IMMEDIATE;"); !r) return r.error();
    auto apply = repo_internal::exec(raw, m.sql);
    if (apply) apply = record_migration(raw, m.name, m.sql);
    if (auto r = repo_internal::exec(raw, apply ? "COMMIT;" : "ROLLBACK;"); !r) {
      if (rebuild) (void)repo_internal::exec(raw, "PRAGMA foreign_keys=ON;");
      return r.error();
    }
    if (rebuild) {
      // 重建后的表结构已由迁移脚本重建外键约束，恢复 PRAGMA 即可。
      if (auto r = repo_internal::exec(raw, "PRAGMA foreign_keys=ON;"); !r) return r.error();
    }
    if (!apply) {
      return Error{ErrorCode::Internal,
                   std::string("迁移失败: ") + m.name + " — " +
                       apply.error().message()};
    }
    impl->applied = i + 1;
  }

  return Database(std::move(impl));
}

Database::Database(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Database::~Database() = default;

Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

sqlite3* Database::raw() const noexcept { return impl_->db.get(); }

bool Database::checkpoint() {
  return sqlite3_wal_checkpoint_v2(impl_->db.get(), nullptr,
                                   SQLITE_CHECKPOINT_FULL, nullptr, nullptr) ==
         SQLITE_OK;
}

std::recursive_mutex& Database::write_mutex() noexcept {
  return impl_->write_mu;
}

std::size_t Database::schema_version() const { return impl_->applied; }

}  // namespace genealogy
