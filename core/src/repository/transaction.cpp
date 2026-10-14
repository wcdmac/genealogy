#include <genealogy/repository/transaction.hpp>

#include <sqlite3.h>

#include "sqlite_util.hpp"

namespace genealogy {

// 不用 using 引入 commit/rollback：会与成员函数 Transaction::commit 撞名。
// 这里显式限定调用。

Transaction::Transaction(Database& db) : db_(db) {
  // 已经在外层事务里：搭车，不重复开，也不负责提交
  if (sqlite3_get_autocommit(db_.raw()) == 0) return;
  if (auto r = repo_internal::begin_immediate(db_.raw()); r) {
    owns_ = true;
  } else {
    start_error_ = r.error();
  }
}

Transaction::~Transaction() {
  // 未提交就析构：回滚，不留半截数据。没开过事务就不能乱滚。
  if (done_ || !owns_) return;
  repo_internal::rollback(db_.raw());
}

Result<void> Transaction::commit() {
  if (done_) return invalid_argument("事务已提交，不能重复提交");
  if (start_error_) return *start_error_;
  if (!owns_) {
    // 搭车的那一段：最外层会提交，这里只宣告本段写完了
    done_ = true;
    return {};
  }
  if (auto r = repo_internal::commit(db_.raw()); !r) return r.error();
  done_ = true;
  return {};
}

}  // namespace genealogy
