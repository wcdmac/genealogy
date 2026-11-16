#include <genealogy/model/person_name.hpp>

namespace genealogy {

std::string to_string(PersonNameType type) {
  switch (type) {
    case PersonNameType::Genealogy: return "genealogy";
    case PersonNameType::Infant: return "infant";
    case PersonNameType::School: return "school";
    case PersonNameType::Renamed: return "renamed";
    case PersonNameType::Posthumous: return "posthumous";
  }
  return "genealogy";
}

std::optional<PersonNameType> person_name_type_from_string(
    const std::string& s) {
  if (s == "genealogy") return PersonNameType::Genealogy;
  if (s == "infant") return PersonNameType::Infant;
  if (s == "school") return PersonNameType::School;
  if (s == "renamed") return PersonNameType::Renamed;
  if (s == "posthumous") return PersonNameType::Posthumous;
  return std::nullopt;
}

nlohmann::json to_json(const PersonName& v) {
  nlohmann::json j;
  j["id"] = v.id;
  j["person_id"] = v.person_id;
  j["name"] = v.name;
  j["name_type"] = to_string(v.type);
  if (v.start_date) j["start_date"] = v.start_date->to_json();
  if (v.end_date) j["end_date"] = v.end_date->to_json();
  j["is_primary"] = v.is_primary;
  if (v.note) j["note"] = *v.note;
  return j;
}

}  // namespace genealogy
