#include <genealogy/model/branch.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

nlohmann::json to_json(const Branch& v) {
  nlohmann::ordered_json j;
  j["id"] = v.id;
  j["lineage_id"] = v.lineage_id;
  j["name"] = v.name;
  if (v.founder_person_id) j["founder_person_id"] = *v.founder_person_id;
  if (v.parent_branch_id) j["parent_branch_id"] = *v.parent_branch_id;
  if (v.generation_char) j["generation_char"] = *v.generation_char;
  if (v.note) j["note"] = *v.note;
  j["rev"] = v.rev;
  return j;
}

}  // namespace genealogy
