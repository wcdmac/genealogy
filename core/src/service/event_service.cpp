#include <genealogy/service/event_service.hpp>

#include <genealogy/ulid.hpp>

namespace genealogy {

EventService::EventService(Database& db)
    : repo_(db),
      persons_(db),
      unions_(db),
      branches_(db),
      lineages_(db),
      places_(db) {}

Error EventService::validate(const Event& e) {
  if (e.subject_id.empty()) return invalid_argument("subject_id 必填");
  if (!is_valid_ulid(e.subject_id)) {
    return invalid_argument("subject_id 不是合法 ULID: " + e.subject_id);
  }

  // 主体存在性：按 subject_type 分派
  switch (e.subject_type) {
    case EventSubjectType::Person: {
      auto subject = persons_.get(e.subject_id);
      if (!subject) return not_found("事件主体（人物）不存在: " + e.subject_id);
      break;
    }
    case EventSubjectType::Union: {
      auto subject = unions_.get(e.subject_id);
      if (!subject) return not_found("事件主体（结合）不存在: " + e.subject_id);
      break;
    }
    case EventSubjectType::Branch: {
      auto subject = branches_.get(e.subject_id);
      if (!subject) return not_found("事件主体（支派）不存在: " + e.subject_id);
      break;
    }
    case EventSubjectType::Lineage: {
      auto subject = lineages_.get(e.subject_id);
      if (!subject) return not_found("事件主体（宗族）不存在: " + e.subject_id);
      break;
    }
  }

  if (e.place_id && !e.place_id->empty()) {
    auto place = places_.get(*e.place_id);
    if (!place) return not_found("事件地点不存在: " + *e.place_id);
  }
  return {};
}

Result<Event> EventService::create_event(const Event& draft) {
  if (auto err = validate(draft); err) return err;
  return repo_.insert(draft);
}

Result<std::vector<Event>> EventService::list_events(
    const EventFilter& filter) {
  return repo_.list(filter);
}

Result<Event> EventService::get_event(const std::string& id) {
  return repo_.get(id);
}

Result<void> EventService::remove_event(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
