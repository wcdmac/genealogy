#include <genealogy/service/lineage_service.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

LineageService::LineageService(Database& db) : db_(&db), repo_(db) {}

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
  // ROADMAP M7.2：删除被引事实时 409——宗族被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  const int n = citations.count_for_subject(CitationSubjectType::Lineage, id);
  if (n > 0) {
    return conflict("宗族被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  return repo_.remove(id);
}

}  // namespace genealogy
