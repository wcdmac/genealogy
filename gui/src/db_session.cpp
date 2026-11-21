#include <genealogy/gui/db_session.hpp>

#include <genealogy/error.hpp>

#include <string>

namespace genealogy::gui {

DbSession::~DbSession() { close(); }

void DbSession::rebuild_services() {
  persons_ = std::make_unique<PersonService>(*db_);
  unions_ = std::make_unique<UnionService>(*db_);
  events_ = std::make_unique<EventService>(*db_);
  books_ = std::make_unique<BookRepository>(*db_);
  branches_ = std::make_unique<BranchService>(*db_);
  places_ = std::make_unique<PlaceService>(*db_);
}

bool DbSession::open(const std::string& path, std::string* why) {
  close();
  auto opened = Database::open(path);
  if (!opened) {
    if (why != nullptr) *why = opened.error().message();
    return false;
  }
  db_ = std::make_unique<Database>(std::move(*opened));
  path_ = path;
  rebuild_services();
  return true;
}

void DbSession::close() {
  persons_.reset();
  unions_.reset();
  events_.reset();
  books_.reset();
  branches_.reset();
  places_.reset();
  db_.reset();
  path_.clear();
}

}  // namespace genealogy::gui
