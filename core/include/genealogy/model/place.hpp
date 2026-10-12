#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

enum class PlaceType {
  Village,
  Town,
  County,
  Prefecture,
  Province,
  Grave,
  AncestralHall,
  Other,
};

std::string to_string(PlaceType type);
std::optional<PlaceType> place_type_from_string(const std::string& s);

// 地点（place 表）：历史地名为主名，modern_admin 挂现代政区对照。
struct Place {
  std::string id;
  std::string name;
  std::optional<PlaceType> type;
  std::optional<std::string> modern_admin;
  std::optional<std::string> historical_name;  // 历代沿革别名
  std::optional<double> lat;
  std::optional<double> lng;
  std::optional<std::string> note;
};

nlohmann::json to_json(const Place& v);

}  // namespace genealogy
