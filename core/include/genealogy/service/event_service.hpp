#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/lineage_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/place_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy {

// 事件用例层。校验：主体必须存在（按 subject_type 分派到对应仓储）；
// 事件地点若填写必须存在。
class EventService {
 public:
  explicit EventService(Database& db);

  Result<Event> create_event(const Event& draft);
  Result<std::vector<Event>> list_events(const EventFilter& filter);
  Result<Event> get_event(const std::string& id);
  Result<void> remove_event(const std::string& id);

 private:
  Error validate(const Event& e);

  EventRepository repo_;
  PersonRepository persons_;
  UnionRepository unions_;
  BranchRepository branches_;
  LineageRepository lineages_;
  PlaceRepository places_;
};

}  // namespace genealogy
