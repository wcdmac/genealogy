#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct LineageFilter {
  std::optional<std::string> surname;
  int limit = 50;
  std::optional<std::string> cursor;
};

class LineageRepository {
 public:
  explicit LineageRepository(Database& db);

  Result<std::vector<Lineage>> list(const LineageFilter& filter) const;
  Result<Lineage> get(const std::string& id) const;
  Result<Lineage> insert(Lineage lineage);
  Result<Lineage> update(const Lineage& lineage);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
