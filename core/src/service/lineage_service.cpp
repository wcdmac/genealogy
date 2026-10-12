#include <genealogy/service/lineage_service.hpp>

namespace genealogy {

LineageService::LineageService(Database& db) : repo_(db) {}

Result<Lineage> LineageService::create_lineage(const Lineage& draft) {
  if (draft.name.empty()) return invalid_argument("谱名必填");
  if (draft.surname.empty()) return invalid_argument("姓必填");
  return repo_.insert(draft);
}

Result<Lineage> LineageService::get_lineage(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Lineage>> LineageService::list_lineages(
    const LineageFilter& filter) {
  return repo_.list(filter);
}

Result<Lineage> LineageService::update_lineage(const Lineage& lineage) {
  if (lineage.id.empty()) return invalid_argument("id 必填");
  if (lineage.name.empty()) return invalid_argument("谱名必填");
  if (lineage.surname.empty()) return invalid_argument("姓必填");
  return repo_.update(lineage);
}

Result<void> LineageService::remove_lineage(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
