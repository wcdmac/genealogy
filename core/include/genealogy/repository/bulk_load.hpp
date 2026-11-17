#pragma once

// 灌库快路径（M1.6 性能验收用）。
//
// 与用例层的分工：
//   · PersonService / UnionService / EventService 管**正确性**——领域不变量、
//     rev、事务边界，一条一条来，适合交互式录入；
//   · 本类管**吞吐**——整个灌库跑在**一个事务**里，四条预备语句复用，
//     没有逐行校验。调用方必须自己保证不变量（外键、父母先于子女等），
//     数据库的 FOREIGN KEY 仍会兜底。
//
// 为什么需要它：10 万人物 + 50 万事件若走用例层，每条 INSERT 自成一个事务，
// WAL 下每次提交都要 fsync，实测慢到无法接受。灌库是批处理，本就该一个事务。

#include <memory>
#include <string>
#include <vector>

struct sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

#include <genealogy/error.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

class BulkLoader {
 public:
  explicit BulkLoader(Database& db);
  ~BulkLoader();   // 未 finish() 而析构 = 回滚，不留半截数据

  BulkLoader(const BulkLoader&) = delete;
  BulkLoader& operator=(const BulkLoader&) = delete;

  // 灌入一条。失败即整体作废（调用方应停止并让对象析构回滚）。
  // 地点必须最先灌：person/union/event 都有 place_id 外键。
  Result<void> add_place(const Place& p);
  Result<void> add_person(const Person& p);
  Result<void> add_union(const Union& u);   // 含 union_member
  Result<void> add_event(const Event& e);
  // 出处链（M7.5）：来源必须先于引用灌入（citation.source_id 外键）。
  // 不做逐行校验——调用方保证（导入器已校验过主体存在性）。
  Result<void> add_source(const Source& s);
  Result<void> add_citation(const Citation& c);

  // 提交。返回后对象不可再用。
  Result<void> finish();

  std::size_t persons() const noexcept { return persons_; }
  std::size_t places() const noexcept { return places_; }
  std::size_t unions() const noexcept { return unions_; }
  std::size_t events() const noexcept { return events_; }
  std::size_t sources() const noexcept { return sources_; }
  std::size_t citations() const noexcept { return citations_; }

 private:
  // 事务在第一次 add_* 时惰性开启：构造函数返回不了 Result，
  // 而「没灌任何东西就析构」不该触发回滚。
  Result<void> ensure_started();
  Result<void> step(sqlite3_stmt* st, const char* what);

  Database& db_;
  bool started_ = false;
  bool locked_ = false;  // M9.1：写互斥（ensure_started 到 finish/析构）
  bool done_ = false;
  std::size_t persons_ = 0;
  std::size_t places_ = 0;
  std::size_t unions_ = 0;
  std::size_t events_ = 0;
  std::size_t sources_ = 0;
  std::size_t citations_ = 0;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace genealogy
