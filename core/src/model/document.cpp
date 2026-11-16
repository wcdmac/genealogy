#include <genealogy/model/document.hpp>

namespace genealogy {

std::string to_string(DocumentType type) {
  switch (type) {
    case DocumentType::Preface: return "preface";
    case DocumentType::Rule: return "rule";
    case DocumentType::Regulation: return "regulation";
    case DocumentType::Literature: return "literature";
    case DocumentType::Contract: return "contract";
    case DocumentType::EditionNote: return "edition_note";
  }
  return "preface";
}

std::optional<DocumentType> document_type_from_string(const std::string& s) {
  if (s == "preface") return DocumentType::Preface;
  if (s == "rule") return DocumentType::Rule;
  if (s == "regulation") return DocumentType::Regulation;
  if (s == "literature") return DocumentType::Literature;
  if (s == "contract") return DocumentType::Contract;
  if (s == "edition_note") return DocumentType::EditionNote;
  return std::nullopt;
}

nlohmann::json to_json(const Document& v) {
  nlohmann::json j;
  j["id"] = v.id;
  j["lineage_id"] = v.lineage_id;
  j["type"] = to_string(v.type);
  j["title"] = v.title;
  if (v.author) j["author"] = *v.author;
  if (v.date_text) j["date_text"] = *v.date_text;
  if (v.edition) j["edition"] = *v.edition;
  if (v.content) j["content"] = *v.content;
  if (v.seq) j["seq"] = *v.seq;
  return j;
}

}  // namespace genealogy
