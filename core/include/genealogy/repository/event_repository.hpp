#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct EventFilter {
  std::optional<EventSubjectType> subject_type;
  std::optional<std::string> subject_id;
  std::optional<EventType> type;
  int limit = 50;
  std::optional<std::string> cursor;
};

// 事件仓储（event 表无 rev：事件为追加型事实，修正以新事件+描述为准）。
class EventRepository {
 public:
  explicit EventRepository(Database& db);

  Result<std::vector<Event>> list(const EventFilter& filter) const;
  Result<Event> get(const std::string& id) const;
  Result<Event> insert(Event event);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
