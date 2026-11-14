#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

struct CitationFilter {
  std::optional<CitationSubjectType> subject_type;
  std::optional<std::string> subject_id;
  std::optional<std::string> source_id;
  int limit = 50;
  std::optional<std::string> cursor;
};

class CitationRepository {
 public:
  explicit CitationRepository(Database& db);

  Result<std::vector<Citation>> list(const CitationFilter& filter) const;
  Result<Citation> get(const std::string& id) const;
  Result<Citation> insert(Citation citation);
  Result<Citation> update(const Citation& citation);
  Result<void> remove(const std::string& id);

  // 删除保护计数（ROADMAP M7.2：删除被引事实时 409）。
  int count_for_subject(CitationSubjectType type,
                        const std::string& subject_id) const;
  int count_for_source(const std::string& source_id) const;

 private:
  Database& db_;
};

}  // namespace genealogy
