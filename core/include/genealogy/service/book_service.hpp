#pragma once

#include <genealogy/error.hpp>
#include <genealogy/model/book.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

// 谱元数据用例层（单谱单库：恒单行）。
class BookService {
 public:
  explicit BookService(Database& db);

  Result<Book> get_book();
  Result<Book> update_book(const Book& book);

 private:
  Database* db_ = nullptr;
  BookRepository repo_;
};

}  // namespace genealogy
