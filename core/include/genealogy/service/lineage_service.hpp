#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/lineage_repository.hpp>

namespace genealogy {

// 宗族用例层。建档必填：谱名、姓。
class LineageService {
 public:
  explicit LineageService(Database& db);

  Result<Lineage> create_lineage(const Lineage& draft);
  Result<Lineage> get_lineage(const std::string& id);
  Result<std::vector<Lineage>> list_lineages(const LineageFilter& filter);
  Result<Lineage> update_lineage(const Lineage& lineage);
  Result<void> remove_lineage(const std::string& id);

 private:
  LineageRepository repo_;
};

}  // namespace genealogy
