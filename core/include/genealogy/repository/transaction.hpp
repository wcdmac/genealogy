#pragma once

#include <optional>

#include <genealogy/error.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

// 显式事务：把一批写操作包进同一个事务。
//
// 为什么要它：服务层每条 INSERT 自成事务（SQLite 自动提交），WAL 下每次
// 提交一次 fsync——本机实测约 50 ms。交互式单条录入无所谓，批处理不能忍：
// 1 万条数据的导入按老路要走 8 分钟以上。
//
// 嵌套感知：若最外层已开着事务（sqlite3_get_autocommit == 0），本对象不
// 重复开、也不负责提交，由最外层那个对象收尾。这样 UnionRepository 内部
// 「结合 + 成员必须同事务」的写法可以原样嵌进外层事务里。
//
// 未 commit() 而析构 = 回滚。批量导入中途失败时不会留下半套谱——
// 这正是导入该有的语义：要么整份进库，要么一份不进。
class Transaction {
 public:
  explicit Transaction(Database& db);
  ~Transaction();

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  // 开启失败的原因（几乎只可能是磁盘/权限问题）。非空时不该继续写。
  const std::optional<Error>& error() const noexcept { return start_error_; }

  // 提交。返回后本对象不再管事。重复提交返回 InvalidArgument。
  Result<void> commit();

 private:
  Database& db_;
  bool owns_ = false;   // 本次由本对象开启（否则只是搭最外层的车）
  bool done_ = false;
  std::optional<Error> start_error_;
};

}  // namespace genealogy
