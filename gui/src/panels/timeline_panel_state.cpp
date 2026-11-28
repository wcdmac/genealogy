// 时间线面板状态机（无 ImGui 依赖）。

#include <genealogy/gui/panels/timeline_panel.hpp>

#include <genealogy/repository/place_repository.hpp>

#include <algorithm>
#include <map>

namespace genealogy::gui {

namespace {
const char* kTypeNames[] = {"全部", "出生", "卒", "葬", "婚姻", "迁徙",
                            "科考", "任职", "荣典", "居址", "注记"};
int type_index(genealogy::EventType t) {
  switch (t) {
    case genealogy::EventType::Birth: return 1;
    case genealogy::EventType::Death: return 2;
    case genealogy::EventType::Burial: return 3;
    case genealogy::EventType::Marriage: return 4;
    case genealogy::EventType::Migration: return 5;
    case genealogy::EventType::Examination: return 6;
    case genealogy::EventType::Office: return 7;
    case genealogy::EventType::Honor: return 8;
    case genealogy::EventType::Residence: return 9;
    case genealogy::EventType::Annotation: return 10;
  }
  return 0;
}
}  // namespace

bool TimelinePanel::reload(DbSession& session, std::string* why) {
  genealogy::EventFilter f;
  f.limit = 20000;
  auto events = session.events().list_events(f);
  if (!events) { *why = events.error().message(); return false; }

  // 人物姓名索引（事件主体多为 person）
  std::map<std::string, std::string> person_names;
  genealogy::PersonFilter pf;
  pf.limit = 200000;
  if (auto persons = session.persons().list_persons(pf))
    for (const auto& p : *persons)
      person_names[p.id] = p.family_name + p.given_name;

  // 地点名索引
  std::map<std::string, std::string> place_names;
  genealogy::PlaceFilter plf;
  plf.limit = 200000;
  if (auto places = session.places().list_places(plf))
    for (const auto& p : *places) place_names[p.id] = p.name;

  rows_.clear();
  std::map<std::string, int> counts;
  for (const auto& e : *events) {
    if (type_filter_ > 0 && type_index(e.type) != type_filter_) continue;
    TimelineRow r;
    r.year = (e.date && e.date->gregorian) ? *e.date->gregorian
             : (e.date && e.date->lunar) ? *e.date->lunar : "—";
    r.type = genealogy::to_string(e.type);
    if (e.subject_type == genealogy::EventSubjectType::Person) {
      auto it = person_names.find(e.subject_id);
      r.subject = it == person_names.end() ? e.subject_id : it->second;
    } else {
      r.subject = e.subject_id;
    }
    if (e.place_id) {
      auto it = place_names.find(*e.place_id);
      r.place = it == place_names.end() ? *e.place_id : it->second;
      counts[*e.place_id]++;
    }
    r.description = e.description.value_or("");
    rows_.push_back(std::move(r));
  }
  // 时间排序：ISO 年份字典序即年份序；"—" 排末尾
  std::stable_sort(rows_.begin(), rows_.end(), [](const TimelineRow& a,
                                                   const TimelineRow& b) {
    if (a.year == "—") return false;
    if (b.year == "—") return true;
    return a.year < b.year;
  });

  places_.clear();
  for (const auto& [id, count] : counts) {
    auto it = place_names.find(id);
    places_.push_back({id, it == place_names.end() ? id : it->second, count});
  }
  std::stable_sort(places_.begin(), places_.end(),
                   [](const PlaceCount& a, const PlaceCount& b) {
                     return a.count > b.count;
                   });
  return true;
}

void TimelinePanel::clear() {
  loaded_db_.clear();
  loaded_ = false;
  rows_.clear();
  places_.clear();
}

}  // namespace genealogy::gui
