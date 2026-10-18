#include <genealogy/model/migration.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

NLOHMANN_JSON_SERIALIZE_ENUM(MigrationSubjectType,
                             {
                                 {MigrationSubjectType::Person, "person"},
                                 {MigrationSubjectType::Branch, "branch"},
                             })

std::string to_string(MigrationSubjectType type) {
  switch (type) {
    case MigrationSubjectType::Person: return "person";
    case MigrationSubjectType::Branch: return "branch";
  }
  return "person";
}

std::optional<MigrationSubjectType> migration_subject_from_string(
    const std::string& s) {
  if (s == "person") return MigrationSubjectType::Person;
  if (s == "branch") return MigrationSubjectType::Branch;
  return std::nullopt;
}

nlohmann::json to_json(const Migration& v) {
  nlohmann::ordered_json j;
  j["id"] = v.id;
  j["subject_type"] = v.subject_type;
  j["subject_id"] = v.subject_id;
  if (v.from_place_id) j["from_place_id"] = *v.from_place_id;
  if (v.to_place_id) j["to_place_id"] = *v.to_place_id;
  if (v.date) j["date"] = v.date->to_json();  // GenealogyDate，与 event 同口径
  if (v.reason) j["reason"] = *v.reason;
  if (v.route) j["route"] = *v.route;
  if (v.note) j["note"] = *v.note;
  return j;
}

}  // namespace genealogy
