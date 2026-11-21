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

  // 删除保护计数（ROADMAP M7.2：删除被引事实时 409）。失败返回
  // Internal 而非折叠成 0：查询错误折叠成「无引用」会让删除保护
  // 静默失效（2026-11 静态审查 H8）。
  Result<int> count_for_subject(CitationSubjectType type,
                                const std::string& subject_id) const;
  Result<int> count_for_source(const std::string& source_id) const;

  // 无证据主体数（ROADMAP M7.3：/stats 的 uncited_facts）——该类型主体
  // 表里 citation 一条都没挂的行数。目前主体表只有 person/event 两类
  // 有独立表；union/migration/branch/lineage 走别的统计口径。
  // 失败返回 Internal 而非折叠成 0：查询错误折叠成 0 会让 /stats 的
  // uncited_facts 静默失真（H8 同纪律，第三轮新-7 补齐最后一个站点）。
  Result<int> count_subjects_without_citation(CitationSubjectType type) const;

 private:
  Database& db_;
};

}  // namespace genealogy
