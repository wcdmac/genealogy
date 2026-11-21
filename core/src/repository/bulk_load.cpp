#include <genealogy/repository/bulk_load.hpp>

#include <sqlite3.h>

#include <utility>

#include "sqlite_util.hpp"

namespace genealogy {

using repo_internal::Stmt;
using repo_internal::bind_citation;
using repo_internal::bind_event;
using repo_internal::bind_source;
using repo_internal::bind_person;
using repo_internal::bind_place;
using repo_internal::bind_union;
using repo_internal::bind_union_member;
using repo_internal::citation_insert_sql;
using repo_internal::event_insert_sql;
using repo_internal::source_insert_sql;
using repo_internal::person_insert_sql;
using repo_internal::place_insert_sql;
using repo_internal::union_insert_sql;
using repo_internal::union_member_insert_sql;

struct BulkLoader::Impl {
  std::unique_ptr<Stmt> place;
  std::unique_ptr<Stmt> person;
  std::unique_ptr<Stmt> union_row;
  std::unique_ptr<Stmt> member;
  std::unique_ptr<Stmt> event;
  std::unique_ptr<Stmt> source;
  std::unique_ptr<Stmt> citation;
};

BulkLoader::BulkLoader(Database& db) : db_(db), impl_(std::make_unique<Impl>()) {
  impl_->place = std::make_unique<Stmt>(db_.raw(), place_insert_sql());
  impl_->person = std::make_unique<Stmt>(db_.raw(), person_insert_sql());
  impl_->union_row = std::make_unique<Stmt>(db_.raw(), union_insert_sql());
  impl_->member = std::make_unique<Stmt>(db_.raw(), union_member_insert_sql());
  impl_->event = std::make_unique<Stmt>(db_.raw(), event_insert_sql());
  impl_->source = std::make_unique<Stmt>(db_.raw(), source_insert_sql());
  impl_->citation = std::make_unique<Stmt>(db_.raw(), citation_insert_sql());
}

BulkLoader::~BulkLoader() {
  // 未提交就析构：回滚，不留半截数据。没开过事务就不能乱滚。
  if (done_ || !started_) {
    if (locked_) db_.write_mutex().unlock();
    return;
  }
  repo_internal::rollback(db_.raw());
  if (locked_) db_.write_mutex().unlock();
}

Result<void> BulkLoader::ensure_started() {
  if (started_) return {};
  // M9.1：灌库事务全程持有写互斥（与 Transaction 同一把递归锁）
  db_.write_mutex().lock();
  locked_ = true;
  if (!impl_->place->ok() || !impl_->person->ok() || !impl_->union_row->ok() ||
      !impl_->member->ok() || !impl_->event->ok()) {
    return Error{ErrorCode::Internal, "灌库预备语句准备失败（SQL 或列名有误）"};
  }
  // 整个灌库一个事务：10 万人物 + 50 万事件若逐条自动提交，WAL 下每次
  // 提交都要 fsync，实测慢到无法接受。灌库是批处理，本就该一个事务。
  if (auto r = repo_internal::begin_immediate(db_.raw()); !r) return r.error();
  started_ = true;
  return {};
}

Result<void> BulkLoader::step(sqlite3_stmt* st, const char* what) {
  if (st == nullptr) {
    return Error{ErrorCode::Internal, std::string("预备语句未就绪: ") + what};
  }
  if (sqlite3_step(st) != SQLITE_DONE) {
    return Error{ErrorCode::Internal, std::string("灌库失败(") + what + "): " +
                                           sqlite3_errmsg(db_.raw())};
  }
  sqlite3_reset(st);
  return {};
}

Result<void> BulkLoader::add_place(const Place& p) {
  if (auto r = ensure_started(); !r) return r.error();
  if (p.id.empty()) return invalid_argument("灌库地点必须带 id");
  if (p.name.empty()) return invalid_argument("灌库地点必须带名称");
  bind_place(impl_->place->get(), p);
  if (auto r = step(impl_->place->get(), "place"); !r) return r.error();
  ++places_;
  return {};
}

Result<void> BulkLoader::add_person(const Person& p) {
  if (auto r = ensure_started(); !r) return r.error();
  if (p.id.empty()) return invalid_argument("灌库人物必须带 id");
  bind_person(impl_->person->get(), p);
  if (auto r = step(impl_->person->get(), "person"); !r) return r.error();
  ++persons_;
  return {};
}

Result<void> BulkLoader::add_union(const Union& u) {
  if (auto r = ensure_started(); !r) return r.error();
  if (u.id.empty()) return invalid_argument("灌库结合必须带 id");
  if (u.members.empty()) return invalid_argument("灌库结合至少一名成员");
  bind_union(impl_->union_row->get(), u);
  if (auto r = step(impl_->union_row->get(), "union"); !r) return r.error();
  for (const auto& m : u.members) {
    bind_union_member(impl_->member->get(), u.id, m);
    if (auto r2 = step(impl_->member->get(), "union_member"); !r2) {
      return r2.error();
    }
  }
  ++unions_;
  return {};
}

Result<void> BulkLoader::add_event(const Event& e) {
  if (auto r = ensure_started(); !r) return r.error();
  if (e.id.empty()) return invalid_argument("灌库事件必须带 id");
  if (e.subject_id.empty()) return invalid_argument("灌库事件必须带 subject_id");
  bind_event(impl_->event->get(), e);
  if (auto r = step(impl_->event->get(), "event"); !r) return r.error();
  ++events_;
  return {};
}

Result<void> BulkLoader::add_source(const Source& s) {
  if (auto r = ensure_started(); !r) return r.error();
  if (s.id.empty()) return invalid_argument("灌库来源必须带 id");
  bind_source(impl_->source->get(), s);
  if (auto r = step(impl_->source->get(), "source"); !r) return r.error();
  ++sources_;
  return {};
}

Result<void> BulkLoader::add_citation(const Citation& c) {
  if (auto r = ensure_started(); !r) return r.error();
  if (c.id.empty()) return invalid_argument("灌库证据引用必须带 id");
  if (c.subject_id.empty()) return invalid_argument("灌库证据引用必须带 subject_id");
  if (c.source_id.empty()) return invalid_argument("灌库证据引用必须带 source_id");
  bind_citation(impl_->citation->get(), c);
  if (auto r = step(impl_->citation->get(), "citation"); !r) return r.error();
  ++citations_;
  return {};
}

Result<void> BulkLoader::finish() {
  if (done_) return invalid_argument("灌库器已提交，不能重复 finish");
  if (auto r = repo_internal::commit(db_.raw()); !r) {
    repo_internal::rollback(db_.raw());  // commit 失败先回滚清场再放锁
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
