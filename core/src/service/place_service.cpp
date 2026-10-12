#include <genealogy/service/place_service.hpp>

namespace genealogy {

PlaceService::PlaceService(Database& db) : repo_(db) {}

Result<Place> PlaceService::create_place(const Place& draft) {
  if (draft.name.empty()) return invalid_argument("地名必填");
  return repo_.insert(draft);
}

Result<Place> PlaceService::get_place(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Place>> PlaceService::list_places(
    const PlaceFilter& filter) {
  return repo_.list(filter);
}

Result<Place> PlaceService::update_place(const Place& place) {
  if (place.id.empty()) return invalid_argument("id 必填");
  if (place.name.empty()) return invalid_argument("地名必填");
  return repo_.update(place);
}

Result<void> PlaceService::remove_place(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
