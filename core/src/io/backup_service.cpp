#include <genealogy/io/backup_service.hpp>

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>

namespace genealogy::io {

namespace {

namespace fs = std::filesystem;

std::string quote_sql(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

}  // namespace

std::string backup_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &seconds);
#else
  localtime_r(&seconds, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;
  char ms_buf[8];
  std::snprintf(ms_buf, sizeof(ms_buf), "-%03lld",
                static_cast<long long>(ms.count()));
  return std::string(buf) + ms_buf;
}

Result<void> snapshot_database(Database& db, const fs::path& path) {
  const std::string sql = "VACUUM INTO " + quote_sql(path.string());
  char* errmsg = nullptr;
  if (sqlite3_exec(db.raw(), sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
    const std::string message = errmsg ? errmsg : "未知错误";
    sqlite3_free(errmsg);
    return Error{ErrorCode::IoError, "快照失败: " + message};
  }
  return {};
}

Result<void> verify_snapshot(const fs::path& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY,
                      nullptr) != SQLITE_OK) {
    const std::string why = db ? sqlite3_errmsg(db) : "无法打开快照";
    if (db) sqlite3_close(db);
    return Error{ErrorCode::IoError, why};
  }
  sqlite3_stmt* stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db, "SELECT count(*) FROM person", -1,
                                    &stmt, nullptr);
  if (rc != SQLITE_OK) {
    const std::string why = sqlite3_errmsg(db);
    sqlite3_close(db);
    return Error{ErrorCode::IoError, why};
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return {};
}

Result<std::string> restore_database(const fs::path& snapshot,
                                     const fs::path& target, bool force) {
  std::error_code ec;
  if (!fs::exists(snapshot, ec))
    return Error{ErrorCode::NotFound, "快照不存在: " + snapshot.string()};
  if (auto v = verify_snapshot(snapshot); !v) return v.error();

  if (fs::exists(target, ec) && !force)
    return Error{ErrorCode::Conflict,
                 "目标库已存在: " + target.string() +
                     "（确认覆盖请显式指定 force，旧库会移开为 .bak）"};

  fs::path moved_main, moved_wal, moved_shm;
  bool moved = false;
  auto rollback = [&]() {
    if (!moved) return;
    if (fs::exists(moved_main, ec)) fs::rename(moved_main, target, ec);
    for (const auto& pair :
         {std::pair{moved_wal, fs::path(target.string() + "-wal")},
          {std::pair{moved_shm, fs::path(target.string() + "-shm")}}}) {
      if (fs::exists(pair.first, ec)) fs::rename(pair.first, pair.second, ec);
    }
  };

  if (fs::exists(target, ec)) {
    const std::string stamp = ".bak-" + backup_timestamp();
    moved_main = fs::path(target.string() + stamp);
    moved_wal = fs::path(target.string() + "-wal" + stamp);
    moved_shm = fs::path(target.string() + "-shm" + stamp);
    fs::rename(target, moved_main, ec);
    if (ec) return Error{ErrorCode::IoError, "移开旧库失败: " + ec.message()};
    moved = true;
    for (const auto& pair :
         {std::pair{fs::path(target.string() + "-wal"), moved_wal},
          {std::pair{fs::path(target.string() + "-shm"), moved_shm}}}) {
      if (fs::exists(pair.first, ec)) {
        fs::rename(pair.first, pair.second, ec);
        if (ec) {
          rollback();
          return Error{ErrorCode::IoError,
                       "移开旧库旁车失败: " + ec.message()};
        }
      }
    }
  }

  fs::copy_file(snapshot, target, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    rollback();
    return Error{ErrorCode::IoError,
                 "恢复失败: " + ec.message() + "（目标: " + target.string() + "）"};
  }
  return moved ? moved_main.string() : std::string{};
}

}  // namespace genealogy::io
