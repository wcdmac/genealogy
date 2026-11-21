// M9.1 单写者验收门（ROADMAP：压测 50 并发写无 database is locked）。
//
// 线程模型走「每线程一个独立连接」（真实部署形态：server 与 CLI/GUI
// 各持连接），50 线程 × 各写 5 人 = 250 人，断言零错误、零 locked。
// 同连接的写互斥由 Database 写递归锁覆盖（Transaction/BulkLoader 全程
// 持锁），跨连接的竞争由 WAL + busy_timeout 排队兜底。

#include <atomic>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-test-" + generate_ulid() + ".db");
}

void cleanup_db(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + "-wal", ec);
  std::filesystem::remove(path.string() + "-shm", ec);
}

}  // namespace

TEST_CASE("写串行化：50 并发写（独立连接）零错误零 locked") {
  constexpr int kWriters = 50;
  constexpr int kPerWriter = 5;

  const auto path = temp_db_path();
  {
    auto opened = Database::open(path);
    REQUIRE(opened);
  }  // 初始化连接先关——写线程各自开新连接（真实部署形态）

  std::atomic<int> failures{0};
  std::atomic<int> locked{0};
  std::atomic<int> inserted{0};

  {
    std::vector<std::thread> threads;
    threads.reserve(kWriters);
    for (int w = 0; w < kWriters; ++w) {
      threads.emplace_back([&, w] {
        auto opened = Database::open(path);
        if (!opened) {
          ++failures;
          return;
        }
        PersonService svc(*opened);
        for (int i = 0; i < kPerWriter; ++i) {
          Person p;
          p.family_name = "并发";
          p.given_name = "写者" + std::to_string(w) + "-" + std::to_string(i);
          p.gender = Gender::Male;
          auto made = svc.create_person(p);
          if (!made) {
            ++failures;
            if (made.error().message().find("locked") != std::string::npos) {
              ++locked;
            }
          } else {
            ++inserted;
          }
        }
      });
    }
    for (auto& t : threads) t.join();
  }

  CHECK(failures.load() == 0);
  CHECK(locked.load() == 0);
  CHECK(inserted.load() == kWriters * kPerWriter);

  // 全部可读（写都在且库完好）
  {
    auto opened = Database::open(path);
    REQUIRE(opened);
    PersonService svc(*opened);
    PersonFilter f;
    f.limit = 100000;
    auto rows = svc.list_persons(f);
    REQUIRE(rows.has_value());
    CHECK(rows->size() == kWriters * kPerWriter);
  }

  cleanup_db(path);
}

TEST_CASE("写串行化：共享连接跨线程不搭车（F2 回归）") {
  // 2026-11 静态审查 F2：autocommit 判定若在拿锁之前，线程 A 开着事务时
  // 线程 B 会读到 autocommit==0 而直接「搭车」——B 的 commit 只置标志，
  // A 回滚即静默丢 B 的写入。修复后先取锁：B 阻塞到 A 收尾，自开事务。
  const auto path = temp_db_path();
  {
    auto opened = Database::open(path);
    REQUIRE(opened);
  }
  {
  auto db = Database::open(path);
  REQUIRE(db);
  PersonService persons(*db);

  // 线程 A：外层事务，写一人后回滚（制造 autocommit==0 窗口 + 覆盖 B）
  std::atomic<bool> a_in_txn{false};
  std::atomic<bool> a_done{false};
  std::atomic<bool> a_tx_failed{false};
  std::thread a([&] {
    {
      Transaction tx(*db);
      if (tx.error()) {
        a_tx_failed = true;
        return;
      }
      a_in_txn = true;
      while (!a_done.load()) {
        std::this_thread::yield();
      }
    }  // 未 commit 析构 = 回滚（Transaction 无显式 rollback，见头注释）
  });

  // 线程 B：等 A 进入事务后再开自己的事务——若仍会搭车，B 的写入
  // 会随 A 的回滚消失
  Result<std::string> b_write = internal("未执行");
  std::thread b([&] {
    while (!a_in_txn.load()) {
      std::this_thread::yield();
    }
    // B 的事务在此阻塞到 A 收尾（锁先于 autocommit 判定）
    Transaction tx(*db);
    if (tx.error()) return;
    Person p;
    p.id = generate_ulid();
    p.family_name = "搭车";
    p.given_name = "回归";
    p.gender = Gender::Male;
    p.rev = 1;
    auto created = persons.create_person(p);
    if (created) b_write = created->id;
    tx.commit();
  });

  // A 在 B 有机会进入前先完成回滚的编排：A 持锁期间 B 阻塞在
  // Transaction 构造；a_done 置位后 A 回滚放锁，B 才开始自己的事务
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  a_done = true;
  a.join();
  b.join();

  REQUIRE(!a_tx_failed.load());
  REQUIRE(b_write.has_value());
  auto got = persons.get_person(*b_write);
  REQUIRE(got.has_value());   // B 的写入必须独立存活，不随 A 回滚消失
  REQUIRE(got->family_name == "搭车");
  }  // db 作用域收口（关连接）
  cleanup_db(path);
}
