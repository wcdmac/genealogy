#include <genealogy/service/source_service.hpp>

#include <string>

#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

SourceService::SourceService(Database& db) : db_(&db), repo_(db) {}

Result<Source> SourceService::create_source(const Source& draft) {
  if (draft.title.empty()) {
    return invalid_argument("title 必填（来源名称）");
  }
  return repo_.insert(draft);
}

Result<Source> SourceService::get_source(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Source>> SourceService::list_sources(
    const SourceFilter& filter) {
  return repo_.list(filter);
}

Result<Source> SourceService::update_source(const Source& src) {
  if (src.title.empty()) {
    return invalid_argument("title 必填（来源名称）");
  }
  return repo_.update(src);
}

Result<void> SourceService::remove_source(const std::string& id) {
  // 来源被 citation 引用时拒绝删除：证据锚点不许悬空
  // （citation.source_id 外键也会拦，这里先给出人话 Conflict）。
  CitationRepository citations(*db_);
  auto cn = citations.count_for_source(id);
  if (!cn) return cn.error();
  const int n = *cn;
  if (n > 0) {
    return conflict("来源被 " + std::to_string(n) +
                    " 条证据引用，先删除引用再删除来源");
  }
  return repo_.remove(id);
}

}  // namespace genealogy
