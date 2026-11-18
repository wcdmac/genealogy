// REST 路由：来源（/sources）与证据引用（/citations）——M7.2 证据链。
//
// citation 按主体过滤（subject_type+subject_id）是主用例；删除保护
// 在服务层（删除被引事实/来源 → Conflict 409），路由只做透传。

#include <genealogy/server/routes_sources.hpp>

#include <genealogy/server/audit.hpp>
#include <genealogy/server/idempotency.hpp>

#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/service/citation_service.hpp>
#include <genealogy/service/source_service.hpp>

namespace genealogy::server {

namespace {

std::string path_id(const drogon::HttpRequestPtr& req) {
  const auto& params = req->getRoutingParameters();
  return params.empty() ? std::string{} : params.front();
}

// 分页（照 routes_entities 口径：limit+cursor）
bool bad_page(const drogon::HttpRequestPtr& req, const std::string& instance,
              Callback& cb, Page* out) {
  std::vector<FieldError> errors;
  *out = parse_page(req, &errors);
  if (!errors.empty()) {
    cb(validation_problem(instance, errors, "分页参数不合法"));
    return true;
  }
  return false;
}

std::optional<CitationSubjectType> subject_type_of(
    const drogon::HttpRequestPtr& req, Callback& cb) {
  const std::string s = req->getParameter("subject_type");
  if (s.empty()) return std::nullopt;
  auto t = citation_subject_from_string(s);
  if (!t) {
    cb(problem_response(400, 1001, "参数校验失败",
                        "subject_type 应为 person/event/union/migration/"
                        "branch/lineage",
                        req->path()));
    return std::nullopt;
  }
  return t;
}

}  // namespace

// ------------------------------------------------------ 来源

void sources_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  SourceService svc(*db);
  SourceFilter f;
  if (const auto s = req->getParameter("type"); !s.empty()) {
    auto t = source_type_from_string(s);
    if (!t) {
      cb(problem_response(400, 1001, "参数校验失败",
                          "type 应为 genealogy_book/local_gazetteer/stele/"
                          "oral/archive/photo/dna/document/other",
                          instance));
      return;
    }
    f.type = *t;
  }
  if (const auto q = req->getParameter("q"); !q.empty()) {
    f.title_contains = q;
  }
  f.limit = page.limit;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  auto rows = svc.list_sources(f);
  if (!rows) {
    cb(error_to_response(rows.error(), instance));
    return;
  }
  const bool more =
      rows->size() > static_cast<std::size_t>(page.limit);
  if (more) rows->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& s : *rows) items.push_back(to_json(s));
  std::string next;
  if (more && !rows->empty()) next = rows->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void sources_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string idem_key = req->getHeader("Idempotency-Key");
  if (auto idem_hit = idempotency_replay(*db, req, idem_key)) {
    cb(idem_hit->response);
    return;
  }
  const std::string instance = req->path();
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req->body());
  } catch (const std::exception& e) {
    cb(problem_response(400, 1001, "参数校验失败",
                        std::string("请求体不是合法 JSON: ") + e.what(),
                        instance));
    return;
  }

  Source draft;
  if (const auto it = body.find("type"); it != body.end() && it->is_string()) {
    auto t = source_type_from_string(it->get<std::string>());
    if (!t) {
      cb(validation_problem(
          instance, {{"type", "应为 genealogy_book/local_gazetteer/stele/"
                             "oral/archive/photo/dna/document/other"}},
          "来源类型不合法"));
      return;
    }
    draft.type = *t;
  }
  if (const auto it = body.find("title"); it != body.end() && it->is_string()) {
    draft.title = it->get<std::string>();
  }
  auto opt_str = [&](const char* k) -> std::optional<std::string> {
    const auto it = body.find(k);
    if (it != body.end() && it->is_string() && !it->get<std::string>().empty()) {
      return it->get<std::string>();
    }
    return std::nullopt;
  };
  draft.author = opt_str("author");
  draft.year_text = opt_str("year_text");
  draft.repository = opt_str("repository");
  draft.uri = opt_str("uri");
  draft.citation_text = opt_str("citation");
  draft.note = opt_str("note");

  SourceService svc(*db);
  auto made = svc.create_source(draft);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  audit_write(*db, req, "source", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j)));
  }
}

void source_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  SourceService svc(*db);
  auto got = svc.get_source(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), std::nullopt));
}

void source_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req->body());
  } catch (const std::exception& e) {
    cb(problem_response(400, 1001, "参数校验失败",
                        std::string("请求体不是合法 JSON: ") + e.what(),
                        instance));
    return;
  }

  SourceService svc(*db);
  auto current = svc.get_source(id);
  if (!current) {
    cb(error_to_response(current.error(), instance));
    return;
  }
  Source updated = *current;
  auto opt_str = [&](const char* k) -> std::optional<std::string> {
    const auto it = body.find(k);
    if (it != body.end() && it->is_string() && !it->get<std::string>().empty()) {
      return it->get<std::string>();
    }
    return std::nullopt;
  };
  if (const auto it = body.find("type"); it != body.end() && it->is_string()) {
    auto t = source_type_from_string(it->get<std::string>());
    if (!t) {
      cb(validation_problem(instance, {{"type", "来源类型不合法"}}, "参数不合法"));
      return;
    }
    updated.type = *t;
  }
  if (const auto it = body.find("title"); it != body.end() && it->is_string()) {
    updated.title = it->get<std::string>();
  }
  updated.author = opt_str("author");
  updated.year_text = opt_str("year_text");
  updated.repository = opt_str("repository");
  updated.uri = opt_str("uri");
  updated.citation_text = opt_str("citation");
  updated.note = opt_str("note");

  auto saved = svc.update_source(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  audit_write(*db, req, "source", updated.id, "update",
              json_diff(to_json(*current), to_json(*saved)));
  cb(json_ok(to_json(*saved), std::nullopt));
}

void source_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  SourceService svc(*db);
  auto before = svc.get_source(path_id(req));  // 审计：删前快照（M8.5）
  auto removed = svc.remove_source(path_id(req));
  if (!removed) {
    cb(error_to_response(removed.error(), req->path()));
    return;
  }
  audit_write(*db, req, "source", path_id(req), "delete",
              before ? to_json(*before) : nlohmann::json::object());
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
}

// ------------------------------------------------------ 证据引用

void citations_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  CitationService svc(*db);
  CitationFilter f;
  if (const auto t = subject_type_of(req, cb); t.has_value()) {
    f.subject_type = *t;
  } else if (!req->getParameter("subject_type").empty()) {
    return;   // subject_type_of 已回 400
  }
  if (const auto s = req->getParameter("subject_id"); !s.empty()) {
    f.subject_id = s;
  }
  if (const auto s = req->getParameter("source_id"); !s.empty()) {
    f.source_id = s;
  }
  f.limit = page.limit;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  auto rows = svc.list_citations(f);
  if (!rows) {
    cb(error_to_response(rows.error(), instance));
    return;
  }
  const bool more =
      rows->size() > static_cast<std::size_t>(page.limit);
  if (more) rows->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& c : *rows) items.push_back(to_json(c));
  std::string next;
  if (more && !rows->empty()) next = rows->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void citations_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string idem_key = req->getHeader("Idempotency-Key");
  if (auto idem_hit = idempotency_replay(*db, req, idem_key)) {
    cb(idem_hit->response);
    return;
  }
  const std::string instance = req->path();
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req->body());
  } catch (const std::exception& e) {
    cb(problem_response(400, 1001, "参数校验失败",
                        std::string("请求体不是合法 JSON: ") + e.what(),
                        instance));
    return;
  }

  Citation draft;
  auto subject_type = [&]() -> std::optional<CitationSubjectType> {
    if (const auto it = body.find("subject_type");
        it != body.end() && it->is_string()) {
      return citation_subject_from_string(it->get<std::string>());
    }
    return std::nullopt;
  }();
  if (body.find("subject_type") != body.end() && !subject_type) {
    cb(validation_problem(instance,
                          {{"subject_type",
                            "应为 person/event/union/migration/branch/lineage"}},
                          "主体类型不合法"));
    return;
  }
  if (!subject_type) {
    cb(validation_problem(instance, {{"subject_type", "必填"}}, "参数校验失败"));
    return;
  }
  draft.subject_type = *subject_type;
  auto str_field = [&](const char* k) -> std::optional<std::string> {
    const auto it = body.find(k);
    if (it != body.end() && it->is_string()) return it->get<std::string>();
    return std::nullopt;
  };
  draft.subject_id = str_field("subject_id").value_or("");
  draft.source_id = str_field("source_id").value_or("");
  draft.locator = str_field("locator");
  draft.note = str_field("note");
  if (const auto it = body.find("confidence");
      it != body.end() && it->is_string()) {
    auto conf = confidence_from_string(it->get<std::string>());
    if (!conf) {
      cb(validation_problem(
          instance,
          {{"confidence", "应为 high / medium / low / disputed"}},
          "置信度不合法"));
      return;
    }
    draft.confidence = *conf;
  }

  CitationService svc(*db);
  auto made = svc.create_citation(draft);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  audit_write(*db, req, "citation", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j)));
  }
}

void citation_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  CitationService svc(*db);
  auto got = svc.get_citation(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), std::nullopt));
}

void citation_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req->body());
  } catch (const std::exception& e) {
    cb(problem_response(400, 1001, "参数校验失败",
                        std::string("请求体不是合法 JSON: ") + e.what(),
                        instance));
    return;
  }

  CitationService svc(*db);
  auto current = svc.get_citation(id);
  if (!current) {
    cb(error_to_response(current.error(), instance));
    return;
  }
  Citation updated = *current;
  auto str_field = [&](const char* k) -> std::optional<std::string> {
    const auto it = body.find(k);
    if (it != body.end() && it->is_string()) return it->get<std::string>();
    return std::nullopt;
  };
  if (const auto it = body.find("subject_type");
      it != body.end() && it->is_string()) {
    auto t = citation_subject_from_string(it->get<std::string>());
    if (!t) {
      cb(validation_problem(instance,
                            {{"subject_type", "主体类型不合法"}}, "参数不合法"));
      return;
    }
    updated.subject_type = *t;
  }
  if (const auto it = body.find("subject_id");
      it != body.end() && it->is_string()) {
    updated.subject_id = it->get<std::string>();
  }
  if (const auto it = body.find("source_id");
      it != body.end() && it->is_string()) {
    updated.source_id = it->get<std::string>();
  }
  updated.locator = str_field("locator");
  if (const auto it = body.find("confidence");
      it != body.end() && it->is_string()) {
    auto conf = confidence_from_string(it->get<std::string>());
    if (!conf) {
      cb(validation_problem(instance, {{"confidence", "置信度不合法"}},
                            "参数不合法"));
      return;
    }
    updated.confidence = *conf;
  }
  updated.note = str_field("note");

  auto saved = svc.update_citation(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  audit_write(*db, req, "citation", updated.id, "update",
              json_diff(to_json(*current), to_json(*saved)));
  cb(json_ok(to_json(*saved), std::nullopt));
}

void citation_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  CitationService svc(*db);
  auto before = svc.get_citation(path_id(req));  // 审计：删前快照（M8.5）
  auto removed = svc.remove_citation(path_id(req));
  if (!removed) {
    cb(error_to_response(removed.error(), req->path()));
    return;
  }
  audit_write(*db, req, "citation", path_id(req), "delete",
              before ? to_json(*before) : nlohmann::json::object());
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
}

}  // namespace genealogy::server
