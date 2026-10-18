#include <genealogy/server/dto.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>

namespace genealogy::server {

namespace {

// 取字段并校验类型。missing 与 null 一视同仁（都算没给），
// 类型不符记 errors 并返回 nullopt——不猜、不静默转换。
bool type_ok(const nlohmann::json& j, const char* key,
             const std::initializer_list<nlohmann::json::value_t>& types,
             std::vector<FieldError>* errors, const char* expect) {
  const auto it = j.find(key);
  if (it == j.end() || it->is_null()) return false;
  for (const auto t : types) {
    if (it->type() == t) return true;
  }
  if (errors != nullptr) {
    errors->push_back({key, std::string("应为 ") + expect});
  }
  return false;
}

}  // namespace

std::optional<std::string> req_string(const nlohmann::json& j,
                                      const char* key,
                                      std::vector<FieldError>* errors) {
  if (!type_ok(j, key, {nlohmann::json::value_t::string}, errors, "字符串")) {
    if (j.find(key) == j.end() || j.find(key)->is_null()) {
      errors->push_back({key, "必填"});
    }
    return std::nullopt;
  }
  const std::string v = j.at(key).get<std::string>();
  if (v.empty()) {
    errors->push_back({key, "不能为空"});
    return std::nullopt;
  }
  return v;
}

std::optional<std::string> opt_string(const nlohmann::json& j, const char* key,
                                      std::vector<FieldError>* errors) {
  if (!type_ok(j, key, {nlohmann::json::value_t::string}, errors, "字符串")) {
    return std::nullopt;
  }
  return j.at(key).get<std::string>();
}

std::optional<int> opt_int(const nlohmann::json& j, const char* key,
                           std::vector<FieldError>* errors) {
  if (!type_ok(j, key, {nlohmann::json::value_t::number_integer,
                        nlohmann::json::value_t::number_unsigned},
               errors, "整数")) {
    return std::nullopt;
  }
  return j.at(key).get<int>();
}

std::optional<bool> opt_bool(const nlohmann::json& j, const char* key,
                             std::vector<FieldError>* errors) {
  if (!type_ok(j, key, {nlohmann::json::value_t::boolean}, errors, "布尔")) {
    return std::nullopt;
  }
  return j.at(key).get<bool>();
}

std::optional<GenealogyDate> opt_date(const nlohmann::json& j,
                                      const char* key,
                                      std::vector<FieldError>* errors) {
  const auto it = j.find(key);
  if (it == j.end() || it->is_null()) return std::nullopt;
  if (!it->is_object()) {
    errors->push_back({key, "应为 GenealogyDate 对象（规范禁止裸日期字符串）"});
    return std::nullopt;
  }
  auto d = GenealogyDate::from_json(*it);
  if (!d) {
    errors->push_back({key, d.error().message()});
    return std::nullopt;
  }
  return *d;
}

// ---------------------------------------------------------------- 请求体

PersonDraft parse_person_create(const nlohmann::json& j) {
  PersonDraft out;
  auto& errs = out.errors;
  if (auto v = req_string(j, "lineage_id", &errs)) out.person.lineage_id = *v;
  if (auto v = req_string(j, "family_name", &errs)) out.person.family_name = *v;
  if (auto v = req_string(j, "given_name", &errs)) out.person.given_name = *v;
  if (auto v = opt_string(j, "branch_id", &errs)) out.person.branch_id = *v;
  if (auto v = opt_string(j, "courtesy_name", &errs)) {
    out.person.courtesy_name = *v;
  }
  if (auto v = opt_string(j, "art_name", &errs)) out.person.art_name = *v;
  if (auto v = opt_string(j, "posthumous_title", &errs)) {
    out.person.posthumous_title = *v;
  }
  if (auto v = opt_string(j, "biography", &errs)) out.person.biography = *v;
  if (auto v = opt_string(j, "father_id", &errs)) out.person.father_id = *v;
  if (auto v = opt_string(j, "mother_id", &errs)) out.person.mother_id = *v;
  if (auto v = opt_int(j, "generation_no", &errs)) {
    out.person.generation_no = *v;
  }
  if (auto v = opt_int(j, "birth_order", &errs)) out.person.birth_order = *v;
  if (auto v = opt_bool(j, "is_heir", &errs)) out.person.is_heir = *v;
  if (auto v = opt_bool(j, "is_living", &errs)) out.person.is_living = *v;
  if (auto v = opt_date(j, "birth_date", &errs)) out.person.birth_date = *v;
  if (auto v = opt_date(j, "death_date", &errs)) out.person.death_date = *v;
  if (const auto it = j.find("gender"); it != j.end() && it->is_string()) {
    auto g = gender_from_string(it->get<std::string>());
    if (g) {
      out.person.gender = *g;
    } else {
      errs.push_back({"gender", "应为 male / female / unknown"});
    }
  } else if (j.find("gender") != j.end() && !j.at("gender").is_null()) {
    errs.push_back({"gender", "应为 male / female / unknown"});
  }
  return out;
}

PersonDraft parse_person_update(const nlohmann::json& j) {
  PersonDraft out;
  auto& errs = out.errors;
  // PATCH 只接受 PersonUpdate 声明的字段；姓/名/lineage 不在其列
  // （改姓改名走 POST /persons/{id}/names），给了也忽略并在 errors 里点明。
  static const char* const kRejected[] = {"family_name", "given_name",
                                          "lineage_id", "id", "rev"};
  for (const char* k : kRejected) {
    if (j.contains(k)) errs.push_back({k, "该字段不可经此端点修改"});
  }
  if (auto v = opt_string(j, "branch_id", &errs)) out.person.branch_id = *v;
  if (auto v = opt_string(j, "courtesy_name", &errs)) {
    out.person.courtesy_name = *v;
  }
  if (auto v = opt_string(j, "art_name", &errs)) out.person.art_name = *v;
  if (auto v = opt_string(j, "posthumous_title", &errs)) {
    out.person.posthumous_title = *v;
  }
  if (auto v = opt_string(j, "biography", &errs)) out.person.biography = *v;
  if (auto v = opt_int(j, "generation_no", &errs)) {
    out.person.generation_no = *v;
  }
  if (auto v = opt_int(j, "birth_order", &errs)) out.person.birth_order = *v;
  if (auto v = opt_bool(j, "is_heir", &errs)) out.person.is_heir = *v;
  if (auto v = opt_bool(j, "is_living", &errs)) out.person.is_living = *v;
  if (auto v = opt_date(j, "birth_date", &errs)) out.person.birth_date = *v;
  if (auto v = opt_date(j, "death_date", &errs)) out.person.death_date = *v;
  if (const auto it = j.find("privacy_level"); it != j.end() &&
      it->is_string()) {
    auto p = privacy_from_string(it->get<std::string>());
    if (p) {
      out.person.privacy = *p;
    } else {
      errs.push_back({"privacy_level", "应为 public / kin / private"});
    }
  }
  return out;
}

LineageDraft parse_lineage_create(const nlohmann::json& j) {
  LineageDraft out;
  auto& errs = out.errors;
  if (auto v = req_string(j, "name", &errs)) out.lineage.name = *v;
  if (auto v = req_string(j, "surname", &errs)) out.lineage.surname = *v;
  if (auto v = opt_string(j, "surname_origin", &errs)) {
    out.lineage.surname_origin = *v;
  }
  if (auto v = opt_string(j, "commandery", &errs)) {
    out.lineage.commandery = *v;
  }
  if (auto v = opt_string(j, "hall_name", &errs)) {
    out.lineage.hall_name = *v;
  }
  if (auto v = opt_string(j, "generation_poem", &errs)) {
    out.lineage.generation_poem = *v;
  }
  return out;
}

LineageDraft parse_lineage_update(const nlohmann::json& j) {
  LineageDraft out;
  auto& errs = out.errors;
  if (auto v = opt_string(j, "name", &errs)) out.lineage.name = *v;
  if (auto v = opt_string(j, "surname_origin", &errs)) {
    out.lineage.surname_origin = *v;
  }
  if (auto v = opt_string(j, "commandery", &errs)) {
    out.lineage.commandery = *v;
  }
  if (auto v = opt_string(j, "hall_name", &errs)) {
    out.lineage.hall_name = *v;
  }
  if (auto v = opt_string(j, "generation_poem", &errs)) {
    out.lineage.generation_poem = *v;
  }
  return out;
}

BranchDraft parse_branch_create(const nlohmann::json& j) {
  BranchDraft out;
  auto& errs = out.errors;
  if (auto v = req_string(j, "lineage_id", &errs)) out.branch.lineage_id = *v;
  if (auto v = req_string(j, "name", &errs)) out.branch.name = *v;
  if (auto v = opt_string(j, "founder_person_id", &errs)) {
    out.branch.founder_person_id = *v;
  }
  if (auto v = opt_string(j, "parent_branch_id", &errs)) {
    out.branch.parent_branch_id = *v;
  }
  if (auto v = opt_string(j, "generation_char", &errs)) {
    out.branch.generation_char = *v;
  }
  return out;
}

PlaceDraft parse_place_create(const nlohmann::json& j) {
  PlaceDraft out;
  auto& errs = out.errors;
  if (auto v = req_string(j, "name", &errs)) out.place.name = *v;
  if (auto v = opt_string(j, "modern_admin", &errs)) {
    out.place.modern_admin = *v;
  }
  if (auto v = opt_string(j, "historical_name", &errs)) {
    out.place.historical_name = *v;
  }
  if (const auto it = j.find("type"); it != j.end() && it->is_string()) {
    // PlaceType 与 openapi 的枚举同名同值，直接用核心的解析器
    auto t = place_type_from_string(it->get<std::string>());
    if (t) {
      out.place.type = *t;
    } else {
      errs.push_back({"type", "应为 village/town/county/prefecture/province/"
                              "grave/ancestral_hall/other"});
    }
  }
  if (const auto it = j.find("lat"); it != j.end() && it->is_number()) {
    out.place.lat = it->get<double>();
  }
  if (const auto it = j.find("lng"); it != j.end() && it->is_number()) {
    out.place.lng = it->get<double>();
  }
  return out;
}

UnionDraft parse_union_create(const nlohmann::json& j) {
  UnionDraft out;
  auto& errs = out.errors;
  if (const auto it = j.find("type"); it != j.end() && it->is_string()) {
    auto t = union_type_from_string(it->get<std::string>());
    if (t) {
      out.uni.type = *t;
    } else {
      errs.push_back({"type", "应为 marriage/concubinage/remarriage"});
    }
  }
  if (auto v = opt_int(j, "sequence", &errs)) out.uni.sequence = *v;
  if (auto v = opt_date(j, "start_date", &errs)) out.uni.start_date = *v;
  if (auto v = opt_date(j, "end_date", &errs)) out.uni.end_date = *v;
  if (auto v = opt_string(j, "place_id", &errs)) out.uni.place_id = *v;
  if (const auto it = j.find("members"); it != j.end() && it->is_array()) {
    for (const auto& m : *it) {
      UnionMember mem;
      if (auto v = req_string(m, "person_id", &errs)) mem.person_id = *v;
      if (const auto rit = m.find("role"); rit != m.end() && rit->is_string()) {
        auto r = member_role_from_string(rit->get<std::string>());
        if (r) {
          mem.role = *r;
        } else {
          errs.push_back({"members[].role", "应为 husband/wife/concubine"});
        }
      } else {
        errs.push_back({"members[].role", "必填"});
      }
      out.uni.members.push_back(std::move(mem));
    }
    if (it->empty()) errs.push_back({"members", "至少一名成员"});
  } else {
    errs.push_back({"members", "必填且应为数组"});
  }
  return out;
}

EventDraft parse_event_create(const nlohmann::json& j) {
  EventDraft out;
  auto& errs = out.errors;
  if (const auto it = j.find("subject_type"); it != j.end() &&
      it->is_string()) {
    auto t = event_subject_from_string(it->get<std::string>());
    if (t) {
      out.event.subject_type = *t;
    } else {
      errs.push_back({"subject_type", "应为 person/union/branch/lineage"});
    }
  } else {
    errs.push_back({"subject_type", "必填"});
  }
  if (auto v = req_string(j, "subject_id", &errs)) out.event.subject_id = *v;
  if (const auto it = j.find("type"); it != j.end() && it->is_string()) {
    auto t = event_type_from_string(it->get<std::string>());
    if (t) {
      out.event.type = *t;
    } else {
      errs.push_back({"type", "应为 birth/death/burial/marriage/migration/"
                              "examination/office/honor/residence/annotation"});
    }
  } else {
    errs.push_back({"type", "必填"});
  }
  if (auto v = opt_date(j, "date", &errs)) out.event.date = *v;
  if (auto v = opt_string(j, "place_id", &errs)) out.event.place_id = *v;
  if (auto v = opt_string(j, "description", &errs)) {
    out.event.description = *v;
  }
  return out;
}

MigrationDraft parse_migration_create(const nlohmann::json& j) {
  MigrationDraft out;
  auto& errs = out.errors;
  if (const auto it = j.find("subject_type"); it != j.end() &&
      it->is_string()) {
    auto t = migration_subject_from_string(it->get<std::string>());
    if (t) {
      out.migration.subject_type = *t;
    } else {
      errs.push_back({"subject_type", "应为 person/branch"});
    }
  } else {
    errs.push_back({"subject_type", "必填"});
  }
  if (auto v = req_string(j, "subject_id", &errs)) {
    out.migration.subject_id = *v;
  }
  if (auto v = opt_string(j, "from_place_id", &errs)) {
    out.migration.from_place_id = *v;
  }
  if (auto v = req_string(j, "to_place_id", &errs)) {
    out.migration.to_place_id = *v;
  }
  // date 按 GenealogyDate 对象接收（规范口径），与 event 同路径
  if (auto v = opt_date(j, "date", &errs)) out.migration.date = *v;
  if (auto v = opt_string(j, "reason", &errs)) out.migration.reason = *v;
  if (auto v = opt_string(j, "route", &errs)) out.migration.route = *v;
  if (auto v = opt_string(j, "note", &errs)) out.migration.note = *v;
  return out;
}

// ---------------------------------------------------------------- 分页

Page parse_page(const drogon::HttpRequestPtr& req,
                std::vector<FieldError>* errors) {
  Page p;
  // limit 上限 200（规范 components/parameters/Limit）
  const std::string lim = req->getParameter("limit");
  if (!lim.empty()) {
    try {
      const long v = std::stol(lim);
      if (v < 1 || v > 200) {
        errors->push_back({"limit", "应在 1..200 之间"});
      } else {
        p.limit = static_cast<int>(v);
      }
    } catch (const std::exception&) {
      errors->push_back({"limit", "应为整数"});
    }
  }
  p.cursor = req->getParameter("cursor");
  return p;
}

nlohmann::json page_envelope(std::vector<nlohmann::json> items,
                             const std::string& next_cursor, bool has_more) {
  nlohmann::json page;
  page["next_cursor"] = next_cursor.empty()
                            ? nlohmann::json(nullptr)
                            : nlohmann::json(next_cursor);
  page["has_more"] = has_more;
  nlohmann::json out;
  out["data"] = std::move(items);
  out["page"] = std::move(page);
  return out;
}

// ---------------------------------------------------------------- ETag

std::optional<std::int64_t> parse_if_match(const drogon::HttpRequestPtr& req) {
  // Drogon 把头名规范化成小写
  const std::string v = req->getHeader("if-match");
  if (v.empty()) return std::nullopt;
  // 规范：ETag 形如 "12"（带双引号）；也宽容接受不带引号的
  std::string s = v;
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  if (s == "*") return std::nullopt;   // 通配不映射到具体版本，调用方另行处理
  try {
    return static_cast<std::int64_t>(std::stol(s));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void set_etag(const drogon::HttpResponsePtr& resp, std::int64_t rev) {
  resp->addHeader("ETag", "\"" + std::to_string(rev) + "\"");
}

// ---------------------------------------------------------------- 响应

drogon::HttpResponsePtr json_response(int status,
                                      const nlohmann::json& body) {
  // 不用 newHttpJsonResponse：Drogon 内部用 JsonCpp（Json::Value），
  // 而全项目（core 的 to_json）统一用 nlohmann。在两者之间转一圈
  // 只会多一层依赖与一份语义差异，故这里序列化成字符串直接设 body。
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(body.dump());
  resp->setContentTypeString("application/json; charset=utf-8");
  resp->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
  return resp;
}

drogon::HttpResponsePtr json_created(const nlohmann::json& body,
                                     std::int64_t rev) {
  auto resp = json_response(201, body);
  set_etag(resp, rev);
  return resp;
}

drogon::HttpResponsePtr json_created(const nlohmann::json& body) {
  return json_response(201, body);
}

drogon::HttpResponsePtr json_ok(const nlohmann::json& body,
                                std::optional<std::int64_t> rev) {
  auto resp = json_response(200, body);
  if (rev) set_etag(resp, *rev);
  return resp;
}

drogon::HttpResponsePtr error_to_response(const Error& err,
                                          const std::string& instance) {
  return problem_response(err, instance);
}

}  // namespace genealogy::server
