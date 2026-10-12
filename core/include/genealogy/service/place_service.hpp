#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/place_repository.hpp>

namespace genealogy {

// 地点用例层。建档必填：地名。
class PlaceService {
 public:
  explicit PlaceService(Database& db);

  Result<Place> create_place(const Place& draft);
  Result<Place> get_place(const std::string& id);
  Result<std::vector<Place>> list_places(const PlaceFilter& filter);
  Result<Place> update_place(const Place& place);
  Result<void> remove_place(const std::string& id);

 private:
  PlaceRepository repo_;
};

}  // namespace genealogy
