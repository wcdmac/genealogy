#include <genealogy/model/lineage.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

nlohmann::json to_json(const Lineage& v) {
  nlohmann::ordered_json j;
  j["id"] = v.id;
  j["name"] = v.name;
  j["surname"] = v.surname;
  if (v.surname_origin) j["surname_origin"] = *v.surname_origin;
  if (v.commandery) j["commandery"] = *v.commandery;
  if (v.hall_name) j["hall_name"] = *v.hall_name;
  if (v.generation_poem) j["generation_poem"] = *v.generation_poem;
  if (v.note) j["note"] = *v.note;
  j["rev"] = v.rev;
  j["created_at"] = v.created_at;
  j["updated_at"] = v.updated_at;
  return j;
}

}  // namespace genealogy
