#include <genealogy/server/routes_entities.hpp>

#include <genealogy/server/audit.hpp>
#include <genealogy/server/idempotency.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <algorithm>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/event_bus.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/service/branch_service.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/lineage_service.hpp>
#include <genealogy/service/migration_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/union_service.hpp>

namespace genealogy::server {

namespace {

// 路径模板参数（如 /persons/{id} 的 id）。
//
// Drogon 的口径：占位符按「位置」匹配——模式里第 n 个 {…} 对应
// getRoutingParameters() 的第 n 个元素，与占位符写什么名字无关
// （{id} 与 {1} 等价）。它**不**进 attributes：早期版本从
// attributes->get<std::string>("id") 取，永远得到空串，
// 症状是所有带 {id} 的路径返回 404、detail 里 id 为空，
// 而路由其实已经匹配上了——这类故障看日志极易误判成路由没注册。
// 所有路由都只有一个占位符，故取 front()。
std::string path_id(const drogon::HttpRequestPtr& req) {
  const auto& params = req->getRoutingParameters();
  return params.empty() ? std::string{} : params.front();
}

// 这五类的列表/详情/更新/删除是同一个模子，差别只在筛选参数与
// 「哪些字段可改」。抽成两个小助手避免五份复制，但关键判定仍就地写。
//
// 分页解析一次到位并交出去：早先的版本在这里解析、调用方再解析一次，
// 第二次还把 std::string* 传给了 std::vector<FieldError>* 形参——
// 那种错能编过才是怪事，故而合并成这一个函数。
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

nlohmann::json parse_body(const drogon::HttpRequestPtr& req,
                          const std::string& instance, bool* ok,
                          Callback& cb) {
  try {
    *ok = true;
    return nlohmann::json::parse(req->body());
  } catch (const std::exception& e) {
    *ok = false;
    cb(bad_json_body(e.what(), instance));
    return nullptr;
  }
}

}  // namespace

// ================================================================ 宗族

void lineages_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  LineageService svc(*db);
  LineageFilter f;
  f.limit = page.limit + 1;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  if (const auto s = req->getParameter("surname"); !s.empty()) f.surname = s;

  auto got = svc.list_lineages(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& v : *got) items.push_back(to_json(v));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void lineages_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
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
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const LineageDraft d = parse_lineage_create(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "宗族建档参数不合法"));
    return;
  }
  LineageService svc(*db);
  auto made = svc.create_lineage(d.lineage);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  audit_write(*db, req, "lineage", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j), made->rev));
  }
  GENEALOGY_PUBLISH_CHANGE("lineage.created", "lineage", made->id, made->rev);
}

void lineage_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  LineageService svc(*db);
  auto got = svc.get_lineage(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), got->rev));
}

void lineage_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const LineageDraft d = parse_lineage_update(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "宗族更新参数不合法"));
    return;
  }

  LineageService svc(*db);
  auto cur = svc.get_lineage(path_id(req));
  if (!cur) {
    cb(error_to_response(cur.error(), instance));
    return;
  }
  const auto want = parse_if_match(req);
  if (!want) {
    cb(problem_response(428, 2009, "需要 If-Match",
                        "更新必须带 If-Match（当前 rev=" +
                            std::to_string(cur->rev) + "）",
                        instance));
    return;
  }

  Lineage updated = *cur;
  if (body.contains("name")) updated.name = d.lineage.name;
  if (body.contains("surname_origin")) {
    updated.surname_origin = d.lineage.surname_origin;
  }
  if (body.contains("commandery")) updated.commandery = d.lineage.commandery;
  if (body.contains("hall_name")) updated.hall_name = d.lineage.hall_name;
  if (body.contains("generation_poem")) {
    updated.generation_poem = d.lineage.generation_poem;
  }
  updated.rev = *want;

  auto saved = svc.update_lineage(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  cb(json_ok(to_json(*saved), saved->rev));
  audit_write(*db, req, "lineage", saved->id, "update",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("lineage.updated", "lineage", saved->id, saved->rev);
}

void lineage_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  LineageService svc(*db);
  const std::string removed_id = path_id(req);
  auto gone = svc.remove_lineage(removed_id);
  if (!gone) {
    cb(error_to_response(gone.error(), req->path()));
    return;
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
  audit_write(*db, req, "lineage", removed_id, "delete",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("lineage.deleted", "lineage", removed_id,
                           std::nullopt);
}

// ================================================================ 支派

void branches_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  BranchService svc(*db);
  BranchFilter f;
  f.limit = page.limit + 1;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  if (const auto s = req->getParameter("lineage_id"); !s.empty()) {
    f.lineage_id = s;
  }

  auto got = svc.list_branches(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& v : *got) items.push_back(to_json(v));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void branches_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
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
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const BranchDraft d = parse_branch_create(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "支派建档参数不合法"));
    return;
  }
  BranchService svc(*db);
  auto made = svc.create_branch(d.branch);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  audit_write(*db, req, "branch", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j), made->rev));
  }
  GENEALOGY_PUBLISH_CHANGE("branch.created", "branch", made->id, made->rev);
}

void branch_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  BranchService svc(*db);
  auto got = svc.get_branch(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), got->rev));
}

void branch_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  BranchService svc(*db);
  auto cur = svc.get_branch(path_id(req));
  if (!cur) {
    cb(error_to_response(cur.error(), instance));
    return;
  }
  const auto want = parse_if_match(req);
  if (!want) {
    cb(problem_response(428, 2009, "需要 If-Match",
                        "更新必须带 If-Match（当前 rev=" +
                            std::to_string(cur->rev) + "）",
                        instance));
    return;
  }

  std::vector<FieldError> errors;
  Branch updated = *cur;
  if (auto v = opt_string(body, "name", &errors)) updated.name = *v;
  if (auto v = opt_string(body, "founder_person_id", &errors)) {
    updated.founder_person_id = *v;
  }
  if (auto v = opt_string(body, "parent_branch_id", &errors)) {
    updated.parent_branch_id = *v;
  }
  if (auto v = opt_string(body, "generation_char", &errors)) {
    updated.generation_char = *v;
  }
  if (!errors.empty()) {
    cb(validation_problem(instance, errors, "支派更新参数不合法"));
    return;
  }
  updated.rev = *want;

  auto saved = svc.update_branch(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  cb(json_ok(to_json(*saved), saved->rev));
  audit_write(*db, req, "branch", saved->id, "update",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("branch.updated", "branch", saved->id, saved->rev);
}

void branch_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  BranchService svc(*db);
  const std::string removed_id = path_id(req);
  auto gone = svc.remove_branch(removed_id);
  if (!gone) {
    cb(error_to_response(gone.error(), req->path()));
    return;
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
  audit_write(*db, req, "branch", removed_id, "delete",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("branch.deleted", "branch", removed_id,
                           std::nullopt);
}

void branch_tree(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);

  BranchService svc(*db);
  auto root = svc.get_branch(id);
  if (!root) {
    cb(error_to_response(root.error(), instance));
    return;
  }
  BranchFilter f;
  f.limit = 100000;
  if (const auto s = req->getParameter("lineage_id"); !s.empty()) {
    f.lineage_id = s;
  } else {
    f.lineage_id = root->lineage_id;
  }
  auto all = svc.list_branches(f);
  if (!all) {
    cb(error_to_response(all.error(), instance));
    return;
  }

  // 规范 BranchNode：{id, name, generation_char, children:[递归]}
  // 父子索引先建一遍：早先的写法是每个节点都全表扫两遍（找自己 + 找孩子），
  // O(n^2)——几百个支派时无感，上万支派就是 10^8 次比较，HTTP 直接超时。
  std::unordered_map<std::string, const Branch*> by_id;
  std::unordered_map<std::string, std::vector<const Branch*>> kids_of;
  by_id.reserve(all->size() * 2);
  for (const auto& b : *all) {
    by_id.emplace(b.id, &b);
    if (b.parent_branch_id) kids_of[*b.parent_branch_id].push_back(&b);
  }

  std::function<nlohmann::json(const std::string&)> emit =
      [&](const std::string& bid) -> nlohmann::json {
        const auto self = by_id.find(bid);
        if (self == by_id.end()) return nullptr;
        const Branch* b = self->second;
        nlohmann::json node;
        node["id"] = b->id;
        node["name"] = b->name;
        node["generation_char"] =
            b->generation_char ? nlohmann::json(*b->generation_char)
                               : nlohmann::json(nullptr);
        nlohmann::json kids = nlohmann::json::array();
        const auto it = kids_of.find(bid);
        if (it != kids_of.end()) {
          for (const auto* c : it->second) kids.push_back(emit(c->id));
        }
        node["children"] = std::move(kids);
        return node;
      };
  cb(json_ok(emit(id), std::nullopt));
}

// ================================================================ 婚姻结合

void unions_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  UnionService svc(*db);
  UnionFilter f;
  f.limit = page.limit + 1;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  if (const auto s = req->getParameter("person_id"); !s.empty()) {
    f.person_id = s;
  }

  auto got = svc.list_unions(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& v : *got) items.push_back(to_json(v));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void unions_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
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
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const UnionDraft d = parse_union_create(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "婚姻结合建档参数不合法"));
    return;
  }
  UnionService svc(*db);
  auto made = svc.create_union(d.uni);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  audit_write(*db, req, "union", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j), made->rev));
  }
  GENEALOGY_PUBLISH_CHANGE("union.created", "union", made->id, made->rev);
}

void union_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  UnionService svc(*db);
  auto got = svc.get_union(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), got->rev));
}

void union_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  UnionService svc(*db);
  auto cur = svc.get_union(path_id(req));
  if (!cur) {
    cb(error_to_response(cur.error(), instance));
    return;
  }
  const auto want = parse_if_match(req);
  if (!want) {
    cb(problem_response(428, 2009, "需要 If-Match",
                        "更新必须带 If-Match（当前 rev=" +
                            std::to_string(cur->rev) + "）",
                        instance));
    return;
  }

  std::vector<FieldError> errors;
  Union updated = *cur;
  if (const auto it = body.find("type"); it != body.end() && it->is_string()) {
    auto t = union_type_from_string(it->get<std::string>());
    if (t) {
      updated.type = *t;
    } else {
      errors.push_back({"type", "应为 marriage/concubinage/remarriage"});
    }
  }
  if (auto v = opt_int(body, "sequence", &errors)) updated.sequence = *v;
  if (auto v = opt_date(body, "start_date", &errors)) {
    updated.start_date = *v;
  }
  if (auto v = opt_date(body, "end_date", &errors)) updated.end_date = *v;
  if (auto v = opt_string(body, "place_id", &errors)) updated.place_id = *v;
  if (!errors.empty()) {
    cb(validation_problem(instance, errors, "婚姻结合更新参数不合法"));
    return;
  }
  updated.rev = *want;

  auto saved = svc.update_union(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  cb(json_ok(to_json(*saved), saved->rev));
  audit_write(*db, req, "union", saved->id, "update",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("union.updated", "union", saved->id, saved->rev);
}

void union_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  UnionService svc(*db);
  const std::string removed_id = path_id(req);
  auto gone = svc.remove_union(removed_id);
  if (!gone) {
    cb(error_to_response(gone.error(), req->path()));
    return;
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
  audit_write(*db, req, "union", removed_id, "delete",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("union.deleted", "union", removed_id,
                           std::nullopt);
}

// ================================================================ 事件

void events_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  EventService svc(*db);
  EventFilter f;
  f.limit = page.limit + 1;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  if (const auto s = req->getParameter("subject_id"); !s.empty()) {
    f.subject_id = s;
  }
  if (const auto s = req->getParameter("subject_type"); !s.empty()) {
    auto t = event_subject_from_string(s);
    if (!t) {
      cb(problem_response(400, 1001, "参数校验失败",
                          "subject_type 应为 person/union/branch/lineage",
                          instance));
      return;
    }
    f.subject_type = *t;
  }

  auto got = svc.list_events(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& v : *got) items.push_back(to_json(v));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void events_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
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
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const EventDraft d = parse_event_create(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "事件建档参数不合法"));
    return;
  }
  EventService svc(*db);
  auto made = svc.create_event(d.event);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  // event 表无 rev（追加型事实），故不设 ETag
  audit_write(*db, req, "event", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j)));
  }
  GENEALOGY_PUBLISH_CHANGE("event.created", "event", made->id, std::nullopt);
}

void event_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  EventService svc(*db);
  auto got = svc.get_event(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), std::nullopt));
}

void event_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  EventService svc(*db);
  const std::string removed_id = path_id(req);
  auto gone = svc.remove_event(removed_id);
  if (!gone) {
    cb(error_to_response(gone.error(), req->path()));
    return;
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
  audit_write(*db, req, "event", removed_id, "delete",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("event.deleted", "event", removed_id,
                           std::nullopt);
}

// ================================================================ 地点

void places_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  PlaceService svc(*db);
  PlaceFilter f;
  f.limit = page.limit + 1;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  if (const auto s = req->getParameter("q"); !s.empty()) f.name_contains = s;

  auto got = svc.list_places(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& v : *got) items.push_back(to_json(v));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void places_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
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
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const PlaceDraft d = parse_place_create(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "地点建档参数不合法"));
    return;
  }
  PlaceService svc(*db);
  auto made = svc.create_place(d.place);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  // place 表无 rev，故不设 ETag
  audit_write(*db, req, "place", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j)));
  }
  GENEALOGY_PUBLISH_CHANGE("place.created", "place", made->id, std::nullopt);
}

void place_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  PlaceService svc(*db);
  auto got = svc.get_place(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  cb(json_ok(to_json(*got), std::nullopt));
}

void place_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  PlaceService svc(*db);
  auto cur = svc.get_place(path_id(req));
  if (!cur) {
    cb(error_to_response(cur.error(), instance));
    return;
  }
  // place 无 rev：不存在乐观锁，故也不要求 If-Match。
  // 要求一个永远对不上的版本号，只会把调用方全逼成 428。

  std::vector<FieldError> errors;
  Place updated = *cur;
  if (auto v = opt_string(body, "name", &errors)) updated.name = *v;
  if (auto v = opt_string(body, "modern_admin", &errors)) {
    updated.modern_admin = *v;
  }
  if (auto v = opt_string(body, "historical_name", &errors)) {
    updated.historical_name = *v;
  }
  if (const auto it = body.find("lat"); it != body.end() && it->is_number()) {
    updated.lat = it->get<double>();
  }
  if (const auto it = body.find("lng"); it != body.end() && it->is_number()) {
    updated.lng = it->get<double>();
  }
  if (!errors.empty()) {
    cb(validation_problem(instance, errors, "地点更新参数不合法"));
    return;
  }

  auto saved = svc.update_place(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  cb(json_ok(to_json(*saved), std::nullopt));   // 无 rev，无 ETag
  audit_write(*db, req, "place", saved->id, "update",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("place.updated", "place", saved->id, std::nullopt);
}

void place_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  PlaceService svc(*db);
  const std::string removed_id = path_id(req);
  auto gone = svc.remove_place(removed_id);
  if (!gone) {
    cb(error_to_response(gone.error(), req->path()));
    return;
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
  audit_write(*db, req, "place", removed_id, "delete",
              nlohmann::json::object());
  GENEALOGY_PUBLISH_CHANGE("place.deleted", "place", removed_id,
                           std::nullopt);
}

// ================================================================ 迁徙

void migrations_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  Page page;
  if (bad_page(req, instance, cb, &page)) return;

  MigrationService svc(*db);

  const std::string branch_id = req->getParameter("branch_id");
  const std::string subject_id = req->getParameter("subject_id");

  // 按支派查 = 支派自身的迁徙 + 其成员个人的迁徙：仓储内子查询原生
  // 解析（MigrationFilter.branch_id），游标与单主体路径同一口径。
  // 此前在本层展开成员 ID 逐成员查询合并——既 N+1，每成员只取一页
  // 的合并口径还会静默丢弃超过一页成员的其余记录。
  MigrationFilter f;
  if (!branch_id.empty()) {
    f.branch_id = branch_id;
  } else if (!subject_id.empty()) {
    f.subject_id = subject_id;
  }
  if (const auto s = req->getParameter("subject_type"); !s.empty()) {
    auto t = migration_subject_from_string(s);
    if (!t) {
      cb(problem_response(400, 1001, "参数校验失败",
                          "subject_type 应为 person/branch", instance));
      return;
    }
    f.subject_type = *t;
  }
  f.limit = page.limit + 1;
  if (!page.cursor.empty()) f.cursor = page.cursor;
  auto got = svc.list_migrations(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  auto rows = std::move(*got);

  const bool more = rows.size() > static_cast<std::size_t>(page.limit);
  if (more) rows.pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& m : rows) items.push_back(to_json(m));
  std::string next;
  if (more && !rows.empty()) next = rows.back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

void migrations_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
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
  bool ok = false;
  const auto body = parse_body(req, instance, &ok, cb);
  if (!ok) return;

  const MigrationDraft d = parse_migration_create(body);
  if (!d.ok()) {
    cb(validation_problem(instance, d.errors, "迁徙登记参数不合法"));
    return;
  }

  MigrationService svc(*db);
  auto made = svc.create_migration(d.migration);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  // migration 表无 rev，故不设 ETag
  audit_write(*db, req, "migration", made->id, "insert", to_json(*made));
  {
    nlohmann::json j = to_json(*made);
    const std::string jbody = j.dump();
    idempotency_store(*db, req, idem_key, 201, jbody);
    cb(json_created(std::move(j)));
  }
  GENEALOGY_PUBLISH_CHANGE("migration.created", "migration", made->id,
                           std::nullopt);
}

}  // namespace genealogy::server
