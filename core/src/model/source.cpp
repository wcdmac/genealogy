#include <genealogy/model/source.hpp>

namespace genealogy {

std::string to_string(SourceType type) {
  switch (type) {
    case SourceType::GenealogyBook: return "genealogy_book";
    case SourceType::LocalGazetteer: return "local_gazetteer";
    case SourceType::Stele: return "stele";
    case SourceType::Oral: return "oral";
    case SourceType::Archive: return "archive";
    case SourceType::Photo: return "photo";
    case SourceType::Dna: return "dna";
    case SourceType::Document: return "document";
    case SourceType::Other: return "other";
  }
  return "other";
}

std::optional<SourceType> source_type_from_string(const std::string& s) {
  if (s == "genealogy_book") return SourceType::GenealogyBook;
  if (s == "local_gazetteer") return SourceType::LocalGazetteer;
  if (s == "stele") return SourceType::Stele;
  if (s == "oral") return SourceType::Oral;
  if (s == "archive") return SourceType::Archive;
  if (s == "photo") return SourceType::Photo;
  if (s == "dna") return SourceType::Dna;
  if (s == "document") return SourceType::Document;
  if (s == "other") return SourceType::Other;
  return std::nullopt;
}

nlohmann::json to_json(const Source& v) {
  nlohmann::json j;
  j["id"] = v.id;
  j["type"] = to_string(v.type);
  j["title"] = v.title;
  if (v.author) j["author"] = *v.author;
  if (v.year_text) j["year_text"] = *v.year_text;
  if (v.repository) j["repository"] = *v.repository;
  if (v.uri) j["uri"] = *v.uri;
  if (v.citation_text) j["citation"] = *v.citation_text;
  if (v.note) j["note"] = *v.note;
  return j;
}

}  // namespace genealogy
