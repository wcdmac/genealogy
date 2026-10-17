#include <genealogy/gui/db_session.hpp>

#include <genealogy/error.hpp>

#include <string>

namespace genealogy::gui {

DbSession::~DbSession() { close(); }

void DbSession::rebuild_services() {
  persons_ = std::make_unique<PersonService>(*db_);
  unions_ = std::make_unique<UnionService>(*db_);
  events_ = std::make_unique<EventService>(*db_);
  lineages_ = std::make_unique<LineageService>(*db_);
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
  // 首次打开的库可能是刚建的空文件：补上默认宗族，
  // 否则后面建人物会因外键失败——桌面端不该让用户看到这种错。
  ensure_default_lineage(*db_);
  rebuild_services();
  return true;
}

void DbSession::close() {
  persons_.reset();
  unions_.reset();
  events_.reset();
  lineages_.reset();
  branches_.reset();
  places_.reset();
  db_.reset();
  path_.clear();
}

}  // namespace genealogy::gui
