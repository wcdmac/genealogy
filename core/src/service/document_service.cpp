#include <genealogy/service/document_service.hpp>

#include <string>
#include <string_view>

#include <genealogy/model/document.hpp>
#include <genealogy/repository/lineage_repository.hpp>

namespace genealogy {

DocumentService::DocumentService(Database& db)
    : db_(&db), repo_(db), lineages_(db) {}

namespace {

// 建档与更新共用的领域校验。空 Error 表示通过。
Error validate_document(const Document& draft, LineageRepository& lineages) {
  if (draft.lineage_id.empty()) return invalid_argument("lineage_id 必填");
  if (draft.title.empty()) return invalid_argument("标题必填");
  auto lineage = lineages.get(draft.lineage_id);
  if (!lineage) return not_found("宗族不存在: " + draft.lineage_id);
  return {};
}

}  // namespace

Result<Document> DocumentService::create_document(const Document& draft) {
  if (auto err = validate_document(draft, lineages_); err) return err;
  return repo_.insert(draft);
}

Result<Document> DocumentService::get_document(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Document>> DocumentService::list_documents(
    const DocumentFilter& filter) {
  return repo_.list(filter);
}

Result<std::vector<Document>> DocumentService::search_documents(
    std::string_view keyword, int limit) {
  return repo_.search(keyword, limit);
}

Result<Document> DocumentService::update_document(const Document& doc) {
  if (auto err = validate_document(doc, lineages_); err) return err;
  return repo_.update(doc);
}

Result<void> DocumentService::remove_document(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
