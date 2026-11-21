#pragma once

// 谱元数据仓储（单谱单库：book 表恒单行，get/update 两操作）。

#include <genealogy/error.hpp>
#include <genealogy/model/book.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

class BookRepository {
 public:
  explicit BookRepository(Database& db);

  Result<Book> get() const;
  Result<Book> update(const Book& book);

 private:
  Database* db_ = nullptr;
};

}  // namespace genealogy
