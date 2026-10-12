#include <genealogy/model/place.hpp>

#include <nlohmann/json.hpp>

namespace genealogy {

NLOHMANN_JSON_SERIALIZE_ENUM(PlaceType, {
                                         {PlaceType::Village, "village"},
                                         {PlaceType::Town, "town"},
                                         {PlaceType::County, "county"},
                                         {PlaceType::Prefecture, "prefecture"},
                                         {PlaceType::Province, "province"},
                                         {PlaceType::Grave, "grave"},
                                         {PlaceType::AncestralHall,
                                          "ancestral_hall"},
                                         {PlaceType::Other, "other"},
                                     })

std::string to_string(PlaceType type) {
  switch (type) {
    case PlaceType::Village: return "village";
    case PlaceType::Town: return "town";
    case PlaceType::County: return "county";
    case PlaceType::Prefecture: return "prefecture";
    case PlaceType::Province: return "province";
    case PlaceType::Grave: return "grave";
    case PlaceType::AncestralHall: return "ancestral_hall";
    case PlaceType::Other: return "other";
  }
  return "other";
}

std::optional<PlaceType> place_type_from_string(const std::string& s) {
  if (s == "village") return PlaceType::Village;
  if (s == "town") return PlaceType::Town;
  if (s == "county") return PlaceType::County;
  if (s == "prefecture") return PlaceType::Prefecture;
  if (s == "province") return PlaceType::Province;
  if (s == "grave") return PlaceType::Grave;
  if (s == "ancestral_hall") return PlaceType::AncestralHall;
  if (s == "other") return PlaceType::Other;
  return std::nullopt;
}

nlohmann::json to_json(const Place& v) {
  nlohmann::ordered_json j;
  j["id"] = v.id;
  j["name"] = v.name;
  if (v.type) j["type"] = *v.type;
  if (v.modern_admin) j["modern_admin"] = *v.modern_admin;
  if (v.historical_name) j["historical_name"] = *v.historical_name;
  if (v.lat) j["lat"] = *v.lat;
  if (v.lng) j["lng"] = *v.lng;
  if (v.note) j["note"] = *v.note;
  return j;
}

}  // namespace genealogy
