#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct SourceFilter {
  std::optional<SourceType> type;
  std::optional<std::string> title_contains;  // 标题模糊匹配
  int limit = 50;
  std::optional<std::string> cursor;
};

class SourceRepository {
 public:
  explicit SourceRepository(Database& db);

  Result<std::vector<Source>> list(const SourceFilter& filter) const;
  Result<Source> get(const std::string& id) const;
  Result<Source> insert(Source source);
  Result<Source> update(const Source& source);
  Result<void> remove(const std::string& id);

 private:
  Database& db_;
};

}  // namespace genealogy
