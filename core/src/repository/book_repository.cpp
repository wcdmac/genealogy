#include <genealogy/repository/book_repository.hpp>

#include <genealogy/util/time.hpp>

#include "sqlite_util.hpp"

#include <sqlite3.h>

namespace genealogy {

namespace {

using repo_internal::col_text;

Book row_to_book(sqlite3_stmt* st) {
  Book b;
  b.name = col_text(st, 0);
  b.surname = col_text(st, 1);
  if (sqlite3_column_type(st, 2) != SQLITE_NULL) {
    b.surname_origin = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
  }
  if (sqlite3_column_type(st, 3) != SQLITE_NULL) {
    b.commandery = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
  }
  if (sqlite3_column_type(st, 4) != SQLITE_NULL) {
    b.hall_name = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
  }
  if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
    b.generation_poem = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
  }
  if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
    b.note = reinterpret_cast<const char*>(sqlite3_column_text(st, 6));
  }
  b.created_at = col_text(st, 7);
  b.updated_at = col_text(st, 8);
  b.rev = sqlite3_column_int(st, 9);
  return b;
}

}  // namespace

BookRepository::BookRepository(Database& db) : db_(&db) {}

Result<Book> BookRepository::get() const {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(),
                         "SELECT name, surname, surname_origin, commandery,"
                         " hall_name, generation_poem, note, created_at,"
                         " updated_at, rev"
                         " FROM book WHERE id = 'book'",
                         -1, &st, nullptr) != SQLITE_OK) {
    return Error{ErrorCode::Internal,
                 std::string("book 查询准备失败: ") + sqlite3_errmsg(db_->raw())};
  }
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    return Error{ErrorCode::Internal, "book 行缺失（schema 异常）"};
  }
  Book b = row_to_book(st);
  sqlite3_finalize(st);
  return b;
}

Result<Book> BookRepository::update(const Book& book) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db_->raw(),
                         "UPDATE book SET name=?1, surname=?2,"
                         " surname_origin=?3, commandery=?4, hall_name=?5,"
                         " generation_poem=?6, note=?7, updated_at=?8, rev=rev+1"
                         " WHERE id = 'book'",
                         -1, &st, nullptr) != SQLITE_OK) {
    return Error{ErrorCode::Internal,
                 std::string("book 更新准备失败: ") + sqlite3_errmsg(db_->raw())};
  }
  sqlite3_bind_text(st, 1, book.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, book.surname.c_str(), -1, SQLITE_TRANSIENT);
  if (book.surname_origin) {
    sqlite3_bind_text(st, 3, book.surname_origin->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, 3);
  }
  if (book.commandery) {
    sqlite3_bind_text(st, 4, book.commandery->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, 4);
  }
  if (book.hall_name) {
    sqlite3_bind_text(st, 5, book.hall_name->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, 5);
  }
  if (book.generation_poem) {
    sqlite3_bind_text(st, 6, book.generation_poem->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, 6);
  }
  if (book.note) {
    sqlite3_bind_text(st, 7, book.note->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(st, 7);
  }
  sqlite3_bind_text(st, 8, utc_now_iso().c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) != SQLITE_DONE) {
    std::string err = sqlite3_errmsg(db_->raw());
    sqlite3_finalize(st);
    return Error{ErrorCode::Internal, "book 更新失败: " + err};
  }
  sqlite3_finalize(st);
  return get();
}

}  // namespace genealogy
