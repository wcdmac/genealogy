#include <genealogy/model/person.hpp>

#include <nlohmann/json.hpp>

#include <genealogy/util/time.hpp>

namespace genealogy {

NLOHMANN_JSON_SERIALIZE_ENUM(Gender, {
                                          {Gender::Male, "male"},
                                          {Gender::Female, "female"},
                                          {Gender::Unknown, "unknown"},
                                      })

NLOHMANN_JSON_SERIALIZE_ENUM(PrivacyLevel, {
                                               {PrivacyLevel::Public, "public"},
                                               {PrivacyLevel::Kin, "kin"},
                                               {PrivacyLevel::Private, "private"},
                                           })

std::string to_string(Gender gender) {
  switch (gender) {
    case Gender::Male: return "male";
    case Gender::Female: return "female";
    case Gender::Unknown: return "unknown";
  }
  return "unknown";
}

std::string to_string(PrivacyLevel level) {
  switch (level) {
    case PrivacyLevel::Public: return "public";
    case PrivacyLevel::Kin: return "kin";
    case PrivacyLevel::Private: return "private";
  }
  return "kin";
}

Result<Gender> gender_from_string(const std::string& s) {
  if (s == "male") return Gender::Male;
  if (s == "female") return Gender::Female;
  if (s == "unknown") return Gender::Unknown;
  return invalid_argument("未知性别: " + s + "（应为 male/female/unknown）");
}

Result<PrivacyLevel> privacy_from_string(const std::string& s) {
  if (s == "public") return PrivacyLevel::Public;
  if (s == "kin") return PrivacyLevel::Kin;
  if (s == "private") return PrivacyLevel::Private;
  return invalid_argument("未知隐私级别: " + s + "（应为 public/kin/private）");
}

nlohmann::json to_json(const Person& p) {
  nlohmann::ordered_json j;
  j["id"] = p.id;
  j["lineage_id"] = p.lineage_id;
  if (p.branch_id) j["branch_id"] = *p.branch_id;
  j["family_name"] = p.family_name;
  j["given_name"] = p.given_name;
  if (p.courtesy_name) j["courtesy_name"] = *p.courtesy_name;
  if (p.art_name) j["art_name"] = *p.art_name;
  if (p.posthumous_title) j["posthumous_title"] = *p.posthumous_title;
  if (p.alias) j["alias"] = *p.alias;
  j["gender"] = p.gender;
  if (p.birth_order) j["birth_order"] = *p.birth_order;
  if (p.generation_no) j["generation_no"] = *p.generation_no;
  j["is_heir"] = p.is_heir;
  j["is_living"] = p.is_living;
  j["privacy_level"] = p.privacy;
  j["birth_date"] = p.birth_date.to_json();
  if (p.death_date) j["death_date"] = p.death_date->to_json();
  if (p.birth_place_id) j["birth_place_id"] = *p.birth_place_id;
  if (p.death_place_id) j["death_place_id"] = *p.death_place_id;
  if (p.burial_place_id) j["burial_place_id"] = *p.burial_place_id;
  if (p.biography) j["biography"] = *p.biography;
  if (p.father_id) j["father_id"] = *p.father_id;
  if (p.mother_id) j["mother_id"] = *p.mother_id;
  if (p.portrait_attachment_id) j["portrait_attachment_id"] = *p.portrait_attachment_id;
  if (p.note) j["note"] = *p.note;
  j["rev"] = p.rev;
  j["created_at"] = p.created_at;
  j["updated_at"] = p.updated_at;
  return j;
}

Result<Person> person_from_json(const nlohmann::json& j) {
  if (!j.is_object()) return invalid_argument("person json 必须是对象");
  Person p;
  try {
    if (const auto it = j.find("id"); it != j.end() && !it->is_null()) {
      p.id = it->get<std::string>();
    }
    if (const auto it = j.find("lineage_id"); it != j.end() && !it->is_null()) {
      p.lineage_id = it->get<std::string>();
    }
    if (const auto it = j.find("branch_id"); it != j.end() && !it->is_null()) {
      p.branch_id = it->get<std::string>();
    }
    const auto family = j.find("family_name");
    const auto given = j.find("given_name");
    if (family == j.end() || !family->is_string() || family->get<std::string>().empty()) {
      return invalid_argument("family_name 必填");
    }
    if (given == j.end() || !given->is_string() || given->get<std::string>().empty()) {
      return invalid_argument("given_name 必填");
    }
    p.family_name = family->get<std::string>();
    p.given_name = given->get<std::string>();
    if (const auto it = j.find("courtesy_name"); it != j.end() && !it->is_null()) {
      p.courtesy_name = it->get<std::string>();
    }
    if (const auto it = j.find("art_name"); it != j.end() && !it->is_null()) {
      p.art_name = it->get<std::string>();
    }
    if (const auto it = j.find("posthumous_title"); it != j.end() && !it->is_null()) {
      p.posthumous_title = it->get<std::string>();
    }
    if (const auto it = j.find("alias"); it != j.end() && !it->is_null()) {
      p.alias = it->get<std::string>();
    }
    if (const auto it = j.find("gender"); it != j.end() && !it->is_null()) {
      auto g = gender_from_string(it->get<std::string>());
      if (!g) return g.error();
      p.gender = *g;
    }
    if (const auto it = j.find("birth_order"); it != j.end() && !it->is_null()) {
      p.birth_order = it->get<int>();
    }
    if (const auto it = j.find("generation_no"); it != j.end() && !it->is_null()) {
      p.generation_no = it->get<int>();
    }
    if (const auto it = j.find("is_heir"); it != j.end() && !it->is_null()) {
      p.is_heir = it->get<bool>();
    }
    if (const auto it = j.find("is_living"); it != j.end() && !it->is_null()) {
      p.is_living = it->get<bool>();
    }
    if (const auto it = j.find("privacy_level"); it != j.end() && !it->is_null()) {
      auto level = privacy_from_string(it->get<std::string>());
      if (!level) return level.error();
      p.privacy = *level;
    }
    if (const auto it = j.find("birth_date"); it != j.end() && !it->is_null()) {
      auto d = GenealogyDate::from_json(*it);
      if (!d) return d.error();
      p.birth_date = *d;
    }
    if (const auto it = j.find("death_date"); it != j.end() && !it->is_null()) {
      auto d = GenealogyDate::from_json(*it);
      if (!d) return d.error();
      p.death_date = *d;
    }
    if (const auto it = j.find("biography"); it != j.end() && !it->is_null()) {
      p.biography = it->get<std::string>();
    }
    if (const auto it = j.find("father_id"); it != j.end() && !it->is_null()) {
      p.father_id = it->get<std::string>();
    }
    if (const auto it = j.find("mother_id"); it != j.end() && !it->is_null()) {
      p.mother_id = it->get<std::string>();
    }
  } catch (const nlohmann::json::exception& e) {
    return invalid_argument(std::string("person json 解析失败: ") + e.what());
  }
  return p;
}

}  // namespace genealogy
