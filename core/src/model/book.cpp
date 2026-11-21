#include <genealogy/model/book.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

nlohmann::json to_json(const Book& v) {
  nlohmann::ordered_json j;
  j["name"] = v.name;
  j["surname"] = v.surname;
  if (v.surname_origin) j["surname_origin"] = *v.surname_origin;
  if (v.commandery) j["commandery"] = *v.commandery;
  if (v.hall_name) j["hall_name"] = *v.hall_name;
  if (v.note) j["note"] = *v.note;
  j["rev"] = v.rev;
  return j;
}

}  // namespace genealogy
