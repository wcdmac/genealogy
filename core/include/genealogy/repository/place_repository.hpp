#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct PlaceFilter {
  std::optional<PlaceType> type;
  std::optional<std::string> name_contains;  // 历史名/现代名模糊匹配
  int limit = 50;
  std::optional<std::string> cursor;
};

class PlaceRepository {
 public:
  explicit PlaceRepository(Database& db);

  Result<std::vector<Place>> list(const PlaceFilter& filter) const;
  Result<Place> get(const std::string& id) const;
  Result<Place> insert(Place place);
  Result<Place> update(const Place& place);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
