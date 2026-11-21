#include <genealogy/repository/generation_poem_repository.hpp>

#include <sqlite3.h>

#include <string>
#include <vector>

#include <genealogy/ulid.hpp>

#include "sqlite_util.hpp"

#include <genealogy/repository/transaction.hpp>

namespace genealogy {
namespace {

using repo_internal::Stmt;
using repo_internal::bind_opt_text;
using repo_internal::col_opt_text;
using repo_internal::col_text;

constexpr const char* kCols =
    "id, owner_type, owner_id, seq, char, note";

enum Col {
  kId = 0,
  kOwnerType,
  kOwnerId,
  kSeq,
  kChar,
  kNote,
};

GenerationPoemEntry row_to_entry(sqlite3_stmt* st) {
  GenerationPoemEntry e;
  e.id = col_text(st, kId);
  e.owner_type = poem_owner_from_string(col_text(st, kOwnerType))
                     .value_or(PoemOwner::Lineage);
  e.owner_id = col_text(st, kOwnerId);
  e.seq = sqlite3_column_int(st, kSeq);
  e.character = col_text(st, kChar);
  e.note = col_opt_text(st, kNote);
  return e;
}

}  // namespace

GenerationPoemRepository::GenerationPoemRepository(Database& db) : db_(db) {}

Result<std::vector<GenerationPoemEntry>> GenerationPoemRepository::list(
    PoemOwner owner, const std::string& owner_id) const {
  Stmt st(db_.raw(),
          "SELECT id, owner_type, owner_id, seq, char, note"
          " FROM generation_poem"
          " WHERE owner_type = ?1 AND owner_id = ?2"
          " ORDER BY seq");
  if (!st.ok()) return internal("准备查询失败");
  sqlite3_bind_text(st.get(), 1, to_string(owner), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st.get(), 2, owner_id.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<GenerationPoemEntry> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    out.push_back(row_to_entry(st.get()));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db_.raw())};
  }
  return out;
}

Result<void> GenerationPoemRepository::replace_all(
    PoemOwner owner, const std::string& owner_id,
    const std::vector<GenerationPoemEntry>& entries) {
  // 一个事务包住删 + 写：派语要么整份换新，要么原样不动。
  // 逐条自动提交的话，中途失败会留下「前几句是新的、后几句是旧的」
  // 这种最坏状态——辈字错位会让整个世系校验跑偏。
  Transaction tx(db_);
  if (tx.error()) return *tx.error();

  {
    Stmt del(db_.raw(),
             "DELETE FROM generation_poem WHERE owner_type = ?1 AND owner_id = ?2");
    if (!del.ok()) return internal("准备删除失败");
    sqlite3_bind_text(del.get(), 1, to_string(owner), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(del.get(), 2, owner_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(del.get()) != SQLITE_DONE) {
      return Error{ErrorCode::Internal,
                   std::string("删除旧派语失败: ") +
                       sqlite3_errmsg(db_.raw())};
    }
  }

  {
    Stmt ins(db_.raw(),
             "INSERT INTO generation_poem(id, owner_type, owner_id, seq, char, note)"
             " VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    if (!ins.ok()) return internal("准备插入失败");
    for (const auto& e : entries) {
      sqlite3_reset(ins.get());
      int i = 1;
      // 归属一律取函数参数，不取条目自带的 owner_type/owner_id 字段——
      // 参数才是权威（调用方说「这些条目属于这个归属」）。曾经按条目字段
      // 绑定，结果「把别归属的旧条目拿来重放」会把数据写回旧归属，
      // 而这种错在单归属用法下永远看不出来。
      const std::string id = e.id.empty() ? generate_ulid() : e.id;
      sqlite3_bind_text(ins.get(), i++, id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins.get(), i++, to_string(owner), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(ins.get(), i++, owner_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(ins.get(), i++, e.seq);
      sqlite3_bind_text(ins.get(), i++, e.character.c_str(), -1, SQLITE_TRANSIENT);
      bind_opt_text(ins.get(), i++, e.note);
      if (sqlite3_step(ins.get()) != SQLITE_DONE) {
        return Error{ErrorCode::Internal,
                     std::string("写入派语失败: ") +
                         sqlite3_errmsg(db_.raw())};
      }
    }
  }

  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
