// M9.1 单写者验收门（ROADMAP：压测 50 并发写无 database is locked）。
//
// 线程模型走「每线程一个独立连接」（真实部署形态：server 与 CLI/GUI
// 各持连接），50 线程 × 各写 5 人 = 250 人，断言零错误、零 locked。
// 同连接的写互斥由 Database 写递归锁覆盖（Transaction/BulkLoader 全程
// 持锁），跨连接的竞争由 WAL + busy_timeout 排队兜底。

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
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
    REQUIRE(ensure_default_lineage(*opened));
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
          p.lineage_id = kDefaultLineageId;
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
