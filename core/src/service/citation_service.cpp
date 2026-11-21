#include <genealogy/service/citation_service.hpp>

#include <string>

#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/migration_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/repository/source_repository.hpp>

namespace genealogy {

CitationService::CitationService(Database& db) : db_(&db), repo_(db) {}

namespace {

// 主体存在性：六类事实都查一遍各自的仓储（低频操作，点查即可）。
Result<void> require_subject_exists(Database& db,
                                    CitationSubjectType type,
                                    const std::string& subject_id) {
  switch (type) {
    case CitationSubjectType::Person: {
      PersonRepository repo(db);
      auto got = repo.get(subject_id);
      if (!got) return not_found("引用主体不存在（person）: " + subject_id);
      return {};
    }
    case CitationSubjectType::Event: {
      EventRepository repo(db);
      auto got = repo.get(subject_id);
      if (!got) return not_found("引用主体不存在（event）: " + subject_id);
      return {};
    }
    case CitationSubjectType::Union: {
      UnionRepository repo(db);
      auto got = repo.get(subject_id);
      if (!got) return not_found("引用主体不存在（union）: " + subject_id);
      return {};
    }
    case CitationSubjectType::Migration: {
      MigrationRepository repo(db);
      auto got = repo.get(subject_id);
      if (!got) return not_found("引用主体不存在（migration）: " + subject_id);
      return {};
    }
    case CitationSubjectType::Branch: {
      BranchRepository repo(db);
      auto got = repo.get(subject_id);
      if (!got) return not_found("引用主体不存在（branch）: " + subject_id);
      return {};
    }
    case CitationSubjectType::Lineage: {
      // 单谱单库：谱本体即 book 行，主体校验恒过（历史数据兼容保留枚举）
      return {};
    }
  }
  return invalid_argument("未知的主体类型");
}

}  // namespace

Result<Citation> CitationService::create_citation(const Citation& draft) {
  if (draft.source_id.empty()) {
    return invalid_argument("source_id 必填（证据必须挂来源）");
  }
  if (draft.subject_id.empty()) {
    return invalid_argument("subject_id 必填");
  }
  if (auto err = require_subject_exists(*db_, draft.subject_type,
                                        draft.subject_id);
      !err) {
    return Error{err.error().code(), err.error().message()};
  }
  // source 存在性：citation.source_id 外键兜底，但先给人话错误
  SourceRepository sources(*db_);
  auto src = sources.get(draft.source_id);
  if (!src) return not_found("来源不存在: " + draft.source_id);
  return repo_.insert(draft);
}

Result<Citation> CitationService::get_citation(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Citation>> CitationService::list_citations(
    const CitationFilter& filter) {
  return repo_.list(filter);
}

Result<Citation> CitationService::update_citation(const Citation& c) {
  if (c.source_id.empty()) {
    return invalid_argument("source_id 必填（证据必须挂来源）");
  }
  if (c.subject_id.empty()) {
    return invalid_argument("subject_id 必填");
  }
  auto current = repo_.get(c.id);
  if (!current) return current.error();
  if (auto err = require_subject_exists(*db_, c.subject_type,
                                        c.subject_id);
      !err) {
    return Error{err.error().code(), err.error().message()};
  }
  SourceRepository sources(*db_);
  auto src = sources.get(c.source_id);
  if (!src) return not_found("来源不存在: " + c.source_id);
  return repo_.update(c);
}

Result<void> CitationService::remove_citation(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
