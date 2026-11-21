#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/document.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/document_repository.hpp>

namespace genealogy {

// 文档用例层。建档必填：lineage_id（宗族须存在）+ type + title。
// remove 无 409 保护——citation 的主体类型不含 document（设计如此，
// 文档不是证据锚点），删除不会悬空任何引用。
class DocumentService {
 public:
  explicit DocumentService(Database& db);

  Result<Document> create_document(const Document& draft);
  Result<Document> get_document(const std::string& id);
  Result<std::vector<Document>> list_documents(const DocumentFilter& filter);
  Result<std::vector<Document>> search_documents(std::string_view keyword,
                                                 int limit);
  Result<Document> update_document(const Document& doc);
  Result<void> remove_document(const std::string& id);

 private:
  Database* db_ = nullptr;
  DocumentRepository repo_;
};

}  // namespace genealogy
