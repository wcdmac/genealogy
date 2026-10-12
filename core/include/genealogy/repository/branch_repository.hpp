#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct BranchFilter {
  std::optional<std::string> lineage_id;
  int limit = 50;
  std::optional<std::string> cursor;
};

class BranchRepository {
 public:
  explicit BranchRepository(Database& db);

  Result<std::vector<Branch>> list(const BranchFilter& filter) const;
  Result<Branch> get(const std::string& id) const;
  Result<Branch> insert(Branch branch);
  Result<Branch> update(const Branch& branch);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
