#pragma once

#include <mutex>
#include <cstddef>
#include <filesystem>
#include <memory>

#include <genealogy/error.hpp>

struct sqlite3;  // 前向声明即可（PImpl 隔离 sqlite3.h）

namespace genealogy {

// SQLite 数据库句柄 + 迁移执行器。
// 打开即应用所有未执行的迁移（shared/schema/*.sql 构建时嵌入二进制）。
class Database {
 public:
  static Result<Database> open(const std::filesystem::path& path);

  ~Database();
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // 写事务互斥（M9.1 单写者队列）：写事务的开启/提交/回滚全程持锁，
  // 同进程内的并发写在此串行化——比「搭车事务」的旧行为严格：
  // 旧逻辑下并发 handler 的嵌套 Transaction 会搭进同一个事务，
  // 事务边界在并发下失效（半套提交/回滚）。递归锁保同线程嵌套
  // （搭车语义在同一线程内保留）；跨线程则排队等锁。
  // 只围写事务，读路径零开销。跨进程竞争仍由 WAL + busy_timeout 兜底。
  std::recursive_mutex& write_mutex() noexcept;

  // 仅供仓储内部使用；调用方不得关闭返回的句柄。
  sqlite3* raw() const noexcept;

  // 已应用的迁移数量。
  std::size_t schema_version() const;

 private:
  struct Impl;
  explicit Database(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace genealogy
