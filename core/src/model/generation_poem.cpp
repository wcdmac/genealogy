#include <genealogy/model/generation_poem.hpp>

namespace genealogy {

const char* to_string(PoemOwner owner) {
  return owner == PoemOwner::Lineage ? "lineage" : "branch";
}

std::optional<PoemOwner> poem_owner_from_string(const std::string& s) {
  if (s == "lineage") return PoemOwner::Lineage;
  if (s == "branch") return PoemOwner::Branch;
  return std::nullopt;
}

std::string poem_owner_key(PoemOwner owner, const std::string& owner_id) {
  return std::string(to_string(owner)) + ":" + owner_id;
}

nlohmann::json to_json(const GenerationPoemEntry& v) {
  nlohmann::json j;
  j["id"] = v.id;
  j["owner_type"] = to_string(v.owner_type);
  j["owner_id"] = v.owner_id;
  j["seq"] = v.seq;
  j["char"] = v.character;
  if (v.note) j["note"] = *v.note;
  return j;
}

}  // namespace genealogy
