#pragma once

// 仓储层内部工具（src 私有，不安装）：SQLite 语句 RAII、列读取、参数绑定。
// 各仓储 cpp 共用；person_repository.cpp 为 M0 期遗留的内联版本，M1 已
// 收敛到本文件（后续维护只改这里）。

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>

namespace genealogy::repo_internal {

// 预备语句 RAII：构造失败时 ok() 为 false（SQL 语法/列名错误时 prepare 即失败）。
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    st_ = sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) == SQLITE_OK ? st_
                                                                     : nullptr;
  }
  ~Stmt() {
    if (st_ != nullptr) sqlite3_finalize(st_);
  }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  sqlite3_stmt* get() const noexcept { return st_; }
  bool ok() const noexcept { return st_ != nullptr; }

 private:
  sqlite3_stmt* st_ = nullptr;
};

std::string col_text(sqlite3_stmt* st, int col);
std::optional<std::string> col_opt_text(sqlite3_stmt* st, int col);
std::optional<int> col_opt_int(sqlite3_stmt* st, int col);
std::optional<std::int64_t> col_opt_int64(sqlite3_stmt* st, int col);
std::optional<double> col_opt_double(sqlite3_stmt* st, int col);

void bind_opt_text(sqlite3_stmt* st, int idx, const std::optional<std::string>& v);
void bind_opt_int(sqlite3_stmt* st, int idx, const std::optional<int>& v);
void bind_opt_double(sqlite3_stmt* st, int idx, const std::optional<double>& v);

// GenealogyDate <-> JSON 文本（与 DDL 的 TEXT 列对应）
void bind_date(sqlite3_stmt* st, int idx, const GenealogyDate& d);
void bind_opt_date(sqlite3_stmt* st, int idx, const std::optional<GenealogyDate>& d);
GenealogyDate col_date(sqlite3_stmt* st, int col);
std::optional<GenealogyDate> col_opt_date(sqlite3_stmt* st, int col);

// FTS5 MATCH 双引号转义（字符串字面量，无语法歧义）
std::string quote_fts(std::string_view keyword);

// "a, b, c" -> "alias.a, alias.b, alias.c"（JOIN 同名列限定用）
std::string qualify_cols(const char* cols, const char* alias);

// 执行无参数 SQL（可含多条以 ; 分隔的语句）：事务控制与 PRAGMA 用。
Result<void> exec(sqlite3* db, const char* sql);

// 事务助手（嵌套使用无意义；失败时返回 Error，由调用方回滚）。
Result<void> begin_immediate(sqlite3* db);
Result<void> commit(sqlite3* db);
Result<void> rollback(sqlite3* db);

}  // namespace genealogy::repo_internal
