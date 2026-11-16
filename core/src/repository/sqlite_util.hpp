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
#include <genealogy/model/event.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/model/union.hpp>

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

// ---- 主表 INSERT：语句文本与参数绑定 ----
// 仓储层与 BulkLoader 共用同一份，避免两处漂移（改列就要改两处的坑）。
// 列序与 DDL 一致，绑定函数按同一顺序取参。

const char* person_insert_sql() noexcept;
const char* place_insert_sql() noexcept;
const char* union_insert_sql() noexcept;
const char* union_member_insert_sql() noexcept;
const char* event_insert_sql() noexcept;
const char* source_insert_sql() noexcept;
const char* citation_insert_sql() noexcept;

// 行 → 绑定参数（不含 id/rev/时间戳的默认值处理，由调用方决定）
void bind_person(sqlite3_stmt* st, const Person& p);
void bind_place(sqlite3_stmt* st, const Place& p);
void bind_union(sqlite3_stmt* st, const Union& u);
void bind_event(sqlite3_stmt* st, const Event& e);
void bind_source(sqlite3_stmt* st, const Source& s);
void bind_citation(sqlite3_stmt* st, const Citation& c);
void bind_union_member(sqlite3_stmt* st, const std::string& union_id,
                       const UnionMember& m);

// FTS5 MATCH 双引号转义（字符串字面量，无语法歧义）
std::string quote_fts(std::string_view keyword);

// "a, b, c" -> "alias.a, alias.b, alias.c"（JOIN 同名列限定用）
std::string qualify_cols(const char* cols, const char* alias);

// 执行无参数 SQL（可含多条以 ; 分隔的语句）：事务控制与 PRAGMA 用。
Result<void> exec(sqlite3* db, const char* sql);

// 事务助手（嵌套使用无意义；失败时返回 Error，由调用方回滚）。
// 事务助手（仅限**最外层 owner** 使用）：嵌套调用方必须改用
// repository/transaction.hpp 的 Transaction 类——那类按「是否本层开启」
// 判断，不会把外层事务提前提交掉。这几个自由函数分不清归属，
// 只适用于 BulkLoader 这种自己就是最外层的场景。
Result<void> begin_immediate(sqlite3* db);
Result<void> commit(sqlite3* db);
Result<void> rollback(sqlite3* db);

}  // namespace genealogy::repo_internal
