#include <genealogy/repository/transaction.hpp>

#include <sqlite3.h>

#include <mutex>

#include "sqlite_util.hpp"

namespace genealogy {

// 不用 using 引入 commit/rollback：会与成员函数 Transaction::commit 撞名。
// 这里显式限定调用。
//
// M9.1 单写者：开启/提交/回滚全程持有 Database 的写递归互斥。并发写
// 在此排队，而不是像旧逻辑那样搭进同一个事务——同连接上并发的嵌套
// Transaction 会让事务边界失效（半套提交/回滚）。递归锁保同线程嵌套
// （搭车语义在同一线程内原样保留）；跨线程则串行。

Transaction::Transaction(Database& db) : db_(db) {
  // 先拿锁、后判 autocommit——顺序不能反。autocommit 是连接的全局状态，
  // 不是线程局部的：若在锁外判定，线程 A 开着事务时线程 B 会读到
  // autocommit==0 而直接「搭车」（B 的 commit 只置标志），A 回滚即静默
  // 丢掉 B 的写入（2026-11 静态审查 F2，macOS 实测路径成立）。先取递归
  // 锁：同线程重入立即成功（真正的搭车，外层事务在持锁中）；跨线程
  // 则阻塞到 A 收尾，此后 autocommit==1，B 开自己的事务。
  db_.write_mutex().lock();
  locked_ = true;
  if (sqlite3_get_autocommit(db_.raw()) == 0) return;   // 同线程搭车（locked_ 已置位，析构/commit 配对释放）
  if (auto r = repo_internal::begin_immediate(db_.raw()); r) {
    owns_ = true;
  } else {
    start_error_ = r.error();
    db_.write_mutex().unlock();  // 开启失败即释放，别把后续写全堵死
    locked_ = false;
  }
}

Transaction::~Transaction() {
  // 未提交就析构：回滚，不留半截数据。没开过事务就不能乱滚。
  if (done_ || !owns_) {
    if (locked_) db_.write_mutex().unlock();
    return;
  }
  repo_internal::rollback(db_.raw());
  if (locked_) db_.write_mutex().unlock();
}

Result<void> Transaction::commit() {
  if (done_) return invalid_argument("事务已提交，不能重复提交");
  if (start_error_) {
    if (locked_) db_.write_mutex().unlock();
    locked_ = false;
    return *start_error_;
  }
  if (!owns_) {
    // 搭车的那一段：最外层会提交，这里只宣告本段写完了
    // （同线程递归锁配对：本段持有一份计数，此处释放）
    done_ = true;
    if (locked_) db_.write_mutex().unlock();
    locked_ = false;
    return {};
  }
  if (auto r = repo_internal::commit(db_.raw()); !r) {
    // commit 失败（BUSY/IOERR）事务仍在连接上开着——先回滚清场再放锁，
    // 否则后续线程会在残留事务上误搭车
    repo_internal::rollback(db_.raw());
    if (locked_) db_.write_mutex().unlock();
    locked_ = false;
    return r.error();
  }
  done_ = true;
  if (locked_) db_.write_mutex().unlock();
  locked_ = false;
  return {};
}

}  // namespace genealogy
