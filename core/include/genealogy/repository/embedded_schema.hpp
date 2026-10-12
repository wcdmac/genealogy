#pragma once

#include <cstddef>

namespace genealogy::embedded_schema {

// 一条迁移脚本（构建时由 cmake/EmbedSql.cmake 从 shared/schema/*.sql 生成）。
struct Migration {
  const char* name;  // 文件名（不含扩展名），如 "0001_init"
  const char* sql;
};

const Migration* migrations() noexcept;
std::size_t migration_count() noexcept;

}  // namespace genealogy::embedded_schema
