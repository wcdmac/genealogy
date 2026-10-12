#pragma once

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
