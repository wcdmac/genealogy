// REST 路由：文档（/documents）——M7.4 谱序/凡例/族规家训/艺文/契约。
//
// 建档校验（lineage 存在、title 必填）在服务层，路由只做透传。
// PATCH 用 merge 语义（absent=保留）：document.content 是大段正文，
// 改标题误清正文不可接受——与 sources 的「absent=清空」不同，
// 见 routes_documents.hpp 注释。

#include <genealogy/server/routes_documents.hpp>

#include <genealogy/server/audit.hpp>
#include <genealogy/server/idempotency.hpp>

#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/document.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/service/document_service.hpp>

namespace genealogy::server {

namespace {

std::string path_id(const drogon::HttpRequestPtr& req) {
  const auto& params = req->getRoutingParameters();
  return params.empty() ? std::string{} : params.front();
}

// 分页（照 routes_sources 口径：limit+cursor）
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

std::optional<DocumentType> type_of(const drogon::HttpRequestPtr& req,
                                    Callback& cb) {
  const std::string s = req->getParameter("type");
  if (s.empty()) return std::nullopt;
  auto t = document_type_from_string(s);
  if (!t) {
    cb(problem_response(400, 1001, "参数校验失败",
                        "type 应为 preface/rule/regulation/literature/"
                        "contract/edition_note",
                        req->path()));
    return std::nullopt;
  }
  return t;
}

}  // namespace

void documents_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  DocumentService svc(*db);
  DocumentFilter f;
  if (const auto s = req->getParameter("lineage_id"); !s.empty()) {
    f.lineage_id = s;
  }
  if (const auto t = type_of(req, cb); t.has_value()) {
    f.type = *t;
  } else if (!req->getParameter("type").empty()) {
    return;  // type_of 已回 400
  }
  f.limit = page.limit;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  auto rows = svc.list_documents(f);
  if (!rows) {
    cb(error_to_response(rows.error(), instance));
    return;
  }
  const bool more = rows->size() > static_cast<std::size_t>(page.limit);
  if (more) rows->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& d : *rows) items.push_back(to_json(d));
  std::string next;
  if (more && !rows->empty()) next = rows->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void documents_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string idem_key = req->getHeader("Idempotency-Key");
  if (auto idem_hit = idempotency_replay(*db, idem_key)) {
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

  Document draft;
  if (const auto it = body.find("lineage_id");
      it != body.end() && it->is_string()) {
    draft.lineage_id = it->get<std::string>();
  }
  if (const auto it = body.find("type"); it != body.end() && it->is_string()) {
    auto t = document_type_from_string(it->get<std::string>());
    if (!t) {
      cb(validation_problem(
          instance,
          {{"type", "应为 preface/rule/regulation/literature/contract/"
                    "edition_note"}},
          "文档类型不合法"));
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
  draft.date_text = opt_str("date_text");
  draft.edition = opt_str("edition");
  draft.content = opt_str("content");
  if (const auto it = body.find("seq"); it != body.end() && it->is_number()) {
    draft.seq = it->get<std::int64_t>();
  }

  DocumentService svc(*db);
  auto made = svc.create_document(draft);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  audit_write(*db, req, "document", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, idem_key, 201, jbody);
    cb(json_created(std::move(j)));
  }
}

void document_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  DocumentService svc(*db);
  auto got = svc.get_document(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), std::nullopt));
}

void document_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
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

  DocumentService svc(*db);
  auto current = svc.get_document(id);
  if (!current) {
    cb(error_to_response(current.error(), instance));
    return;
  }
  // merge 语义：只更新请求里出现的字段，其余保持——content 是大段
  // 正文，「PATCH 改标题误清正文」不可接受（与 sources 的 absent=清空
  // 不同，缘由见 routes_documents.hpp）。
  Document updated = *current;
  if (const auto it = body.find("lineage_id");
      it != body.end() && it->is_string()) {
    updated.lineage_id = it->get<std::string>();
  }
  if (const auto it = body.find("type"); it != body.end() && it->is_string()) {
    auto t = document_type_from_string(it->get<std::string>());
    if (!t) {
      cb(validation_problem(instance, {{"type", "文档类型不合法"}},
                            "参数不合法"));
      return;
    }
    updated.type = *t;
  }
  if (const auto it = body.find("title"); it != body.end() && it->is_string()) {
    updated.title = it->get<std::string>();
  }
  // 可选字段：显式给 null/空串才清空，absent 保持
  auto patch_optional = [&](const char* k, std::optional<std::string>& field) {
    const auto it = body.find(k);
    if (it != body.end()) {
      field = (it->is_string() && !it->get<std::string>().empty())
                  ? std::optional<std::string>{it->get<std::string>()}
                  : std::nullopt;
    }
  };
  patch_optional("author", updated.author);
  patch_optional("date_text", updated.date_text);
  patch_optional("edition", updated.edition);
  patch_optional("content", updated.content);
  if (const auto it = body.find("seq"); it != body.end()) {
    if (it->is_number()) {
      updated.seq = it->get<std::int64_t>();
    } else {
      updated.seq = std::nullopt;  // 显式 null 清除篇序
    }
  }

  auto saved = svc.update_document(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  audit_write(*db, req, "document", updated.id, "update",
              json_diff(to_json(*current), to_json(*saved)));
  cb(json_ok(to_json(*saved), std::nullopt));
}

void document_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  DocumentService svc(*db);
  auto before = svc.get_document(path_id(req));  // 审计：删前快照（M8.5）
  auto removed = svc.remove_document(path_id(req));
  if (!removed) {
    cb(error_to_response(removed.error(), req->path()));
    return;
  }
  audit_write(*db, req, "document", path_id(req), "delete",
              before ? to_json(*before) : nlohmann::json::object());
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
}

}  // namespace genealogy::server
