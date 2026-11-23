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
  // prepare 只读 schema，不读表数据——损坏的数据页不会被发现。
  // 必须 step 一次真正读行，并执行 quick_check 覆盖全库页。
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM person", -1, &stmt, nullptr)
      != SQLITE_OK) {
    const std::string why = sqlite3_errmsg(db);
    sqlite3_close(db);
    return Error{ErrorCode::IoError, why};
  }
  const int step_rc = sqlite3_step(stmt);
  const int fin_rc = sqlite3_finalize(stmt);
  if (step_rc != SQLITE_ROW || fin_rc != SQLITE_OK) {
    const std::string why = sqlite3_errmsg(db);
    sqlite3_close(db);
    return Error{ErrorCode::IoError, "快照数据损坏: " + why};
  }
  stmt = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &stmt, nullptr) ==
      SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const auto* text = sqlite3_column_text(stmt, 0);
      const std::string result = text ? reinterpret_cast<const char*>(text) : "";
      if (result != "ok") {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return Error{ErrorCode::IoError, "快照完整性检查失败: " + result};
      }
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return {};
}

Result<std::string> restore_database(const fs::path& snapshot,
                                     const fs::path& target, bool force) {
  std::error_code ec;
  if (!fs::exists(snapshot, ec))
    return Error{ErrorCode::NotFound, "快照不存在: " + snapshot.string()};
  if (ec)
    return Error{ErrorCode::IoError,
                 "检查快照失败: " + ec.message()};
  if (auto v = verify_snapshot(snapshot); !v) return v.error();

  if (fs::exists(target, ec) && !force)
    return Error{ErrorCode::Conflict,
                 "目标库已存在: " + target.string() +
                     "（确认覆盖请显式指定 force，旧库会移开为 .bak）"};

  const fs::path wal = fs::path(target.string() + "-wal");
  const fs::path shm = fs::path(target.string() + "-shm");
  // 主库不存在但残留 WAL/SHM 时必须拒绝或清理，否则新库打开会回放旧事务。
  if (!fs::exists(target, ec) &&
      (fs::exists(wal, ec) || fs::exists(shm, ec))) {
    if (!force)
      return Error{ErrorCode::Conflict,
                   "目标库旁车文件残留（-wal/-shm），请确认后使用 force 清理"};
  }

  fs::path moved_main, moved_wal, moved_shm;
  bool moved = false;
  auto rollback = [&]() -> std::string {
    std::string err;
    // 先删除可能残缺的新目标，再移回旧库——rename 覆盖既有文件在
    // 标准中无强保证，显式删除是唯一可靠的做法。
    if (fs::exists(target, ec)) fs::remove(target, ec);
    if (fs::exists(wal, ec)) fs::remove(wal, ec);
    if (fs::exists(shm, ec)) fs::remove(shm, ec);
    if (moved) {
      if (fs::exists(moved_main, ec)) {
        fs::rename(moved_main, target, ec);
        if (ec) err += "旧库移回失败: " + ec.message() + "；";
      }
      for (const auto& pair :
           {std::pair{moved_wal, wal}, std::pair{moved_shm, shm}}) {
        if (fs::exists(pair.first, ec)) {
          fs::rename(pair.first, pair.second, ec);
          if (ec) err += "旁车移回失败: " + ec.message() + "；";
        }
      }
    }
    return err;
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
         {std::pair{wal, moved_wal}, std::pair{shm, moved_shm}}) {
      if (fs::exists(pair.first, ec)) {
        fs::rename(pair.first, pair.second, ec);
        if (ec) {
          const std::string rb = rollback();
          return Error{ErrorCode::IoError,
                       "移开旧库旁车失败: " + ec.message() +
                           (rb.empty() ? "" : "；回滚: " + rb)};
        }
      }
    }
  } else if (force) {
    // 目标不存在但旁车残留：force 时一并移走（不直接删，留回滚点）
    const std::string stamp = ".bak-" + backup_timestamp();
    if (fs::exists(wal, ec)) {
      moved_wal = fs::path(wal.string() + stamp);
      fs::rename(wal, moved_wal, ec);
      if (ec) return Error{ErrorCode::IoError, "移开残留 WAL 失败: " + ec.message()};
    }
    if (fs::exists(shm, ec)) {
      moved_shm = fs::path(shm.string() + stamp);
      fs::rename(shm, moved_shm, ec);
      if (ec) return Error{ErrorCode::IoError, "移开残留 SHM 失败: " + ec.message()};
    }
  }

  fs::copy_file(snapshot, target, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    const std::string rb = rollback();
    return Error{ErrorCode::IoError,
                 "恢复失败: " + ec.message() + "（目标: " + target.string() +
                     (rb.empty() ? "" : "；回滚: " + rb) + "）"};
  }
  return moved ? moved_main.string() : std::string{};
}

}  // namespace genealogy::io
