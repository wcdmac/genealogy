#include <genealogy/model/event.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

NLOHMANN_JSON_SERIALIZE_ENUM(EventSubjectType, {
                                                   {EventSubjectType::Person,
                                                    "person"},
                                                   {EventSubjectType::Union,
                                                    "union"},
                                                   {EventSubjectType::Branch,
                                                    "branch"},
                                                   {EventSubjectType::Lineage,
                                                    "lineage"},
                                               })

NLOHMANN_JSON_SERIALIZE_ENUM(EventType, {
                                            {EventType::Birth, "birth"},
                                            {EventType::Death, "death"},
                                            {EventType::Burial, "burial"},
                                            {EventType::Marriage, "marriage"},
                                            {EventType::Migration,
                                             "migration"},
                                            {EventType::Examination,
                                             "examination"},
                                            {EventType::Office, "office"},
                                            {EventType::Honor, "honor"},
                                            {EventType::Residence,
                                             "residence"},
                                            {EventType::Annotation,
                                             "annotation"},
                                        })

std::string to_string(EventSubjectType type) {
  switch (type) {
    case EventSubjectType::Person: return "person";
    case EventSubjectType::Union: return "union";
    case EventSubjectType::Branch: return "branch";
    case EventSubjectType::Lineage: return "lineage";
  }
  return "person";
}

std::string to_string(EventType type) {
  switch (type) {
    case EventType::Birth: return "birth";
    case EventType::Death: return "death";
    case EventType::Burial: return "burial";
    case EventType::Marriage: return "marriage";
    case EventType::Migration: return "migration";
    case EventType::Examination: return "examination";
    case EventType::Office: return "office";
    case EventType::Honor: return "honor";
    case EventType::Residence: return "residence";
    case EventType::Annotation: return "annotation";
  }
  return "annotation";
}

std::optional<EventSubjectType> event_subject_from_string(const std::string& s) {
  if (s == "person") return EventSubjectType::Person;
  if (s == "union") return EventSubjectType::Union;
  if (s == "branch") return EventSubjectType::Branch;
  if (s == "lineage") return EventSubjectType::Lineage;
  return std::nullopt;
}

std::optional<EventType> event_type_from_string(const std::string& s) {
  if (s == "birth") return EventType::Birth;
  if (s == "death") return EventType::Death;
  if (s == "burial") return EventType::Burial;
  if (s == "marriage") return EventType::Marriage;
  if (s == "migration") return EventType::Migration;
  if (s == "examination") return EventType::Examination;
  if (s == "office") return EventType::Office;
  if (s == "honor") return EventType::Honor;
  if (s == "residence") return EventType::Residence;
  if (s == "annotation") return EventType::Annotation;
  return std::nullopt;
}

nlohmann::json to_json(const Event& v) {
  nlohmann::ordered_json j;
  j["id"] = v.id;
  j["subject_type"] = v.subject_type;
  j["subject_id"] = v.subject_id;
  j["type"] = v.type;
  if (v.date) j["date"] = v.date->to_json();
  if (v.place_id) j["place_id"] = *v.place_id;
  if (v.description) j["description"] = *v.description;
  j["created_at"] = v.created_at;
  return j;
}

}  // namespace genealogy
