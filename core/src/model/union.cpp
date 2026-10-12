#include <genealogy/model/union.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

NLOHMANN_JSON_SERIALIZE_ENUM(UnionType, {
                                            {UnionType::Marriage, "marriage"},
                                            {UnionType::Concubinage,
                                             "concubinage"},
                                            {UnionType::Remarriage,
                                             "remarriage"},
                                        })

NLOHMANN_JSON_SERIALIZE_ENUM(UnionStatus, {
                                              {UnionStatus::Active, "active"},
                                              {UnionStatus::Dissolved,
                                               "dissolved"},
                                          })

NLOHMANN_JSON_SERIALIZE_ENUM(MemberRole, {
                                             {MemberRole::Husband, "husband"},
                                             {MemberRole::Wife, "wife"},
                                             {MemberRole::Concubine,
                                              "concubine"},
                                         })

std::string to_string(UnionType type) {
  switch (type) {
    case UnionType::Marriage: return "marriage";
    case UnionType::Concubinage: return "concubinage";
    case UnionType::Remarriage: return "remarriage";
  }
  return "marriage";
}

std::string to_string(UnionStatus status) {
  switch (status) {
    case UnionStatus::Active: return "active";
    case UnionStatus::Dissolved: return "dissolved";
  }
  return "active";
}

std::string to_string(MemberRole role) {
  switch (role) {
    case MemberRole::Husband: return "husband";
    case MemberRole::Wife: return "wife";
    case MemberRole::Concubine: return "concubine";
  }
  return "wife";
}

std::optional<UnionType> union_type_from_string(const std::string& s) {
  if (s == "marriage") return UnionType::Marriage;
  if (s == "concubinage") return UnionType::Concubinage;
  if (s == "remarriage") return UnionType::Remarriage;
  return std::nullopt;
}

std::optional<UnionStatus> union_status_from_string(const std::string& s) {
  if (s == "active") return UnionStatus::Active;
  if (s == "dissolved") return UnionStatus::Dissolved;
  return std::nullopt;
}

std::optional<MemberRole> member_role_from_string(const std::string& s) {
  if (s == "husband") return MemberRole::Husband;
  if (s == "wife") return MemberRole::Wife;
  if (s == "concubine") return MemberRole::Concubine;
  return std::nullopt;
}

nlohmann::json to_json(const Union& v) {
  nlohmann::ordered_json j;
  j["id"] = v.id;
  j["type"] = v.type;
  j["status"] = v.status;
  if (v.sequence) j["sequence"] = *v.sequence;
  if (v.start_date) j["start_date"] = v.start_date->to_json();
  if (v.end_date) j["end_date"] = v.end_date->to_json();
  if (v.place_id) j["place_id"] = *v.place_id;
  if (v.note) j["note"] = *v.note;
  j["members"] = nlohmann::json::array();
  for (const auto& m : v.members) {
    nlohmann::ordered_json mj;
    mj["person_id"] = m.person_id;
    mj["role"] = m.role;
    j["members"].push_back(mj);
  }
  j["rev"] = v.rev;
  return j;
}

}  // namespace genealogy
