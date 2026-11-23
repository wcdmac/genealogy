#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace genealogy {

// citation 的主体类型（citation.subject_type，schema CHECK 同口径）：
// 六类事实皆可挂证据（设计文档 4.8 citation 一等公民）。
enum class CitationSubjectType {
  Person,
  Event,
  Union,
  Migration,
  Branch,
  Lineage,
  Book,
};

std::string to_string(CitationSubjectType type);
std::optional<CitationSubjectType> citation_subject_from_string(
    const std::string& s);

// 置信度：high=原卷直读 / medium=转述推定 / low=孤证存疑 / disputed=诸说矛盾
// （设计文档 4.8 的置信分层；「disputed」即 M7.3 冲突裁定队列的入口）。
enum class Confidence {
  High,
  Medium,
  Low,
  Disputed,
};

std::string to_string(Confidence c);
std::optional<Confidence> confidence_from_string(const std::string& s);

// 证据引用（citation 表）：把某主体的某条事实挂到某个来源。
// locator = 定位串，自由文本（卷/页/字段路径，如「卷三 p136」「birth_date」）。
// citation 表无 rev（同 source，溯源元数据）。
struct Citation {
  std::string id;
  CitationSubjectType subject_type = CitationSubjectType::Person;
  std::string subject_id;
  std::string source_id;
  std::optional<std::string> locator;
  Confidence confidence = Confidence::Medium;
  std::optional<std::string> note;
};

nlohmann::json to_json(const Citation& v);

}  // namespace genealogy
