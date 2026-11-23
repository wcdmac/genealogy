#include <genealogy/model/citation.hpp>

namespace genealogy {

std::string to_string(CitationSubjectType type) {
  switch (type) {
    case CitationSubjectType::Person: return "person";
    case CitationSubjectType::Event: return "event";
    case CitationSubjectType::Union: return "union";
    case CitationSubjectType::Migration: return "migration";
    case CitationSubjectType::Branch: return "branch";
    case CitationSubjectType::Lineage: return "lineage";
    case CitationSubjectType::Book: return "book";
  }
  return "person";
}

std::optional<CitationSubjectType> citation_subject_from_string(
    const std::string& s) {
  if (s == "person") return CitationSubjectType::Person;
  if (s == "event") return CitationSubjectType::Event;
  if (s == "union") return CitationSubjectType::Union;
  if (s == "migration") return CitationSubjectType::Migration;
  if (s == "branch") return CitationSubjectType::Branch;
  if (s == "lineage") return CitationSubjectType::Lineage;
  if (s == "book") return CitationSubjectType::Book;
  return std::nullopt;
}

std::string to_string(Confidence c) {
  switch (c) {
    case Confidence::High: return "high";
    case Confidence::Medium: return "medium";
    case Confidence::Low: return "low";
    case Confidence::Disputed: return "disputed";
  }
  return "medium";
}

std::optional<Confidence> confidence_from_string(const std::string& s) {
  if (s == "high") return Confidence::High;
  if (s == "medium") return Confidence::Medium;
  if (s == "low") return Confidence::Low;
  if (s == "disputed") return Confidence::Disputed;
  return std::nullopt;
}

nlohmann::json to_json(const Citation& v) {
  nlohmann::json j;
  j["id"] = v.id;
  j["subject_type"] = to_string(v.subject_type);
  j["subject_id"] = v.subject_id;
  j["source_id"] = v.source_id;
  if (v.locator) j["locator"] = *v.locator;
  j["confidence"] = to_string(v.confidence);
  if (v.note) j["note"] = *v.note;
  return j;
}

}  // namespace genealogy
