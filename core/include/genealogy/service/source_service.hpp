#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/source_repository.hpp>

namespace genealogy {

// 来源用例层。建档必填：type + title。
// remove：被 citation 引用时 Conflict（409）——证据锚点不许悬空。
class SourceService {
 public:
  explicit SourceService(Database& db);

  Result<Source> create_source(const Source& draft);
  Result<Source> get_source(const std::string& id);
  Result<std::vector<Source>> list_sources(const SourceFilter& filter);
  Result<Source> update_source(const Source& src);
  Result<void> remove_source(const std::string& id);

 private:
  Database* db_ = nullptr;
  SourceRepository repo_;
};

}  // namespace genealogy
