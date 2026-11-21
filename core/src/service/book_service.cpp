#include <genealogy/service/book_service.hpp>

namespace genealogy {

BookService::BookService(Database& db) : db_(&db), repo_(db) {}

Result<Book> BookService::get_book() { return repo_.get(); }

Result<Book> BookService::update_book(const Book& book) {
  if (book.name.empty()) return invalid_argument("谱名必填");
  if (book.surname.empty()) return invalid_argument("主姓必填");
  return repo_.update(book);
}

}  // namespace genealogy
