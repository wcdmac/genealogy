#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

// 证据引用用例层（设计文档 4.8：citation 一等公民）。
// create：主体（六类）与来源都必须存在——NotFound；confidence 四档。
// 被引事实删除保护在各实体的 remove 侧实现（409）。
class CitationService {
 public:
  explicit CitationService(Database& db);

  Result<Citation> create_citation(const Citation& draft);
  Result<Citation> get_citation(const std::string& id);
  Result<std::vector<Citation>> list_citations(const CitationFilter& filter);
  Result<Citation> update_citation(const Citation& c);
  Result<void> remove_citation(const std::string& id);

 private:
  Database* db_ = nullptr;
  CitationRepository repo_;
};

}  // namespace genealogy
