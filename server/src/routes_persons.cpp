#include <genealogy/server/routes_persons.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/service/person_service.hpp>

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

// 深度参数：规范默认 10，范围 1..50
bool parse_depth(const drogon::HttpRequestPtr& req, int* out,
                 std::vector<FieldError>* errors) {
  const std::string s = req->getParameter("depth");
  if (s.empty()) {
    *out = 10;
    return true;
  }
  try {
    const long v = std::stol(s);
    if (v < 1 || v > 50) {
      errors->push_back({"depth", "应在 1..50 之间"});
      return false;
    }
    *out = static_cast<int>(v);
    return true;
  } catch (const std::exception&) {
    errors->push_back({"depth", "应为整数"});
    return false;
  }
}

// 世系算法的公共前置：装载全库快照。M1 规模足够；
// 更大规模下改为按需子图装载（设计文档 4.4.1 预留）。
lineage::PersonGraph load_graph(Database& db, std::string* why) {
  PersonRepository persons(db);
  PersonFilter pf;
  pf.limit = 200000;
  auto rows = persons.list(pf);
  if (!rows) {
    *why = rows.error().message();
    return {};
  }
  UnionRepository unions(db);
  UnionFilter uf;
  uf.limit = 200000;
  auto urows = unions.list(uf);
  if (!urows) {
    *why = urows.error().message();
    return {};
  }
  // 零拷贝移入：图自持数据（见 graph.hpp 生命周期说明与 routes_misc 同款注释）。
  // 曾经的 build(*rows, *urows) 让图里全是悬空指针。
  return lineage::PersonGraph::build(std::move(*rows), std::move(*urows));
}

}  // namespace

// ---------------------------------------------------------------- 列表

void persons_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();

  std::vector<FieldError> errors;
  const Page page = parse_page(req, &errors);
  if (!errors.empty()) {
    cb(validation_problem(instance, errors, "分页参数不合法"));
    return;
  }

  PersonService svc(*db);
  PersonFilter f;
  f.limit = page.limit + 1;   // 多取一条判 has_more
  if (!page.cursor.empty()) f.cursor = page.cursor;

  // 结构化筛选：openapi 的 /persons 查询参数
  const std::string branch = req->getParameter("branch_id");
  if (!branch.empty()) f.branch_id = branch;
  const std::string lineage = req->getParameter("lineage_id");
  if (!lineage.empty()) f.lineage_id = lineage;
  if (const auto g = req->getParameter("generation_from"); !g.empty()) {
    try {
      f.generation_from = std::stoi(g);
    } catch (const std::exception&) {
      cb(validation_problem(instance, {{"generation_from", "应为整数"}}, ""));
      return;
    }
  }
  if (const auto g = req->getParameter("generation_to"); !g.empty()) {
    try {
      f.generation_to = std::stoi(g);
    } catch (const std::exception&) {
      cb(validation_problem(instance, {{"generation_to", "应为整数"}}, ""));
      return;
    }
  }

  const std::string q = req->getParameter("q");
  if (!q.empty()) {
    // 全文检索走 FTS，与结构化筛选互斥（openapi 未定义组合语义）
    auto got = svc.search_persons(q, f.limit);
    if (!got) {
      cb(error_to_response(got.error(), instance));
      return;
    }
    std::vector<nlohmann::json> items;
    const bool more = got->size() > static_cast<std::size_t>(page.limit);
    if (more) got->pop_back();
    for (const auto& p : *got) items.push_back(to_json(p));
    std::string next;
    if (more && !got->empty()) next = got->back().id;
    cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
    return;
  }

  auto got = svc.list_persons(f);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  items.reserve(got->size());
  for (const auto& p : *got) items.push_back(to_json(p));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

// ---------------------------------------------------------------- 建档

void persons_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
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

  const PersonDraft draft = parse_person_create(body);
  if (!draft.ok()) {
    cb(validation_problem(instance, draft.errors, "人物建档参数不合法"));
    return;
  }

  PersonService svc(*db);
  auto made = svc.create_person(draft.person);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  cb(json_created(to_json(*made), made->rev));
}

// ---------------------------------------------------------------- 详情

void person_get(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();

  PersonService svc(*db);
  auto got = svc.get_person(path_id(req));
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  cb(json_ok(to_json(*got), got->rev));
}

// ---------------------------------------------------------------- 更新

void person_patch(const drogon::HttpRequestPtr& req, Callback&& cb) {
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

  const PersonDraft draft = parse_person_update(body);
  if (!draft.ok()) {
    cb(validation_problem(instance, draft.errors, "人物更新参数不合法"));
    return;
  }

  PersonService svc(*db);
  auto current = svc.get_person(id);
  if (!current) {
    cb(error_to_response(current.error(), instance));
    return;
  }

  // If-Match 缺失时不允许盲写：并发编辑下会把别人的改动覆盖掉。
  // 规范 5.2.1 要求写操作带 If-Match，这里按缺失即 409 处理。
  const auto want = parse_if_match(req);
  if (!want) {
    cb(problem_response(428, 2009, "需要 If-Match",
                        "更新必须带 If-Match（当前 rev=" +
                            std::to_string(current->rev) + "）",
                        instance));
    return;
  }

  Person updated = *current;
  const Person& d = draft.person;
  // PATCH 语义：只覆盖请求里给出的字段。parse_person_update 对未给的
  // 字段留默认值，故用「是否出现在 JSON 里」判定，而非值是否非空。
  if (body.contains("branch_id")) updated.branch_id = d.branch_id;
  if (body.contains("courtesy_name")) updated.courtesy_name = d.courtesy_name;
  if (body.contains("art_name")) updated.art_name = d.art_name;
  if (body.contains("posthumous_title")) {
    updated.posthumous_title = d.posthumous_title;
  }
  if (body.contains("biography")) updated.biography = d.biography;
  if (body.contains("generation_no")) updated.generation_no = d.generation_no;
  if (body.contains("birth_order")) updated.birth_order = d.birth_order;
  if (body.contains("is_heir")) updated.is_heir = d.is_heir;
  if (body.contains("is_living")) updated.is_living = d.is_living;
  if (body.contains("birth_date")) updated.birth_date = d.birth_date;
  if (body.contains("death_date")) updated.death_date = d.death_date;
  if (body.contains("privacy_level")) updated.privacy = d.privacy;

  updated.id = current->id;
  updated.rev = *want;   // 仓储层据此做乐观锁比对

  auto saved = svc.update_person(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  cb(json_ok(to_json(*saved), saved->rev));
}

// ---------------------------------------------------------------- 删除

void person_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  PersonService svc(*db);
  auto gone = svc.remove_person(path_id(req));
  if (!gone) {
    cb(error_to_response(gone.error(), instance));
    return;
  }
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
}

// ---------------------------------------------------------------- 祖先链

void person_ancestors(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);

  std::vector<FieldError> errors;
  int depth = 10;
  if (!parse_depth(req, &depth, &errors)) {
    cb(validation_problem(instance, errors, ""));
    return;
  }

  PersonService svc(*db);
  auto me = svc.get_person(id);
  if (!me) {
    cb(error_to_response(me.error(), instance));
    return;
  }

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }

  // 规范 AncestorTree：{person, generations:[{depth, persons:[…]}]}
  nlohmann::json out;
  out["person"] = to_json(*me);
  nlohmann::json gens = nlohmann::json::array();
  std::vector<const Person*> frontier{&*me};
  for (int d = 1; d <= depth && !frontier.empty(); ++d) {
    std::vector<const Person*> next;
    for (const auto* p : frontier) {
      if (const auto* f = graph.father_of(p->id)) next.push_back(f);
      if (const auto* m = graph.mother_of(p->id)) next.push_back(m);
    }
    // 去重（同父同母的半亲会在两条路径上重复出现）
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    if (next.empty()) break;
    nlohmann::json row;
    row["depth"] = d;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* p : next) arr.push_back(to_json(*p));
    row["persons"] = std::move(arr);
    gens.push_back(std::move(row));
    frontier = std::move(next);
  }
  out["generations"] = std::move(gens);
  cb(json_ok(out, std::nullopt));
}

// ---------------------------------------------------------------- 子孙树

void person_descendants(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);

  std::vector<FieldError> errors;
  int depth = 10;
  if (!parse_depth(req, &depth, &errors)) {
    cb(validation_problem(instance, errors, ""));
    return;
  }

  PersonService svc(*db);
  auto me = svc.get_person(id);
  if (!me) {
    cb(error_to_response(me.error(), instance));
    return;
  }

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }

  auto tree = lineage::descendants_tree(graph, id, depth);
  if (!tree) {
    cb(error_to_response(tree.error(), instance));
    return;
  }

  // 规范 DescendantTree：{person, children:[递归]}
  std::function<nlohmann::json(const lineage::TreeNode&)> emit =
      [&](const lineage::TreeNode& n) -> nlohmann::json {
        nlohmann::json j;
        j["person"] = n.person ? to_json(*n.person) : nlohmann::json(nullptr);
        nlohmann::json kids = nlohmann::json::array();
        for (const auto& c : n.children) kids.push_back(emit(c));
        j["children"] = std::move(kids);
        return j;
      };

  nlohmann::json out;
  out["person"] = to_json(*me);
  nlohmann::json kids = nlohmann::json::array();
  for (const auto& c : tree->children) kids.push_back(emit(c));
  out["children"] = std::move(kids);
  cb(json_ok(out, std::nullopt));
}

// ---------------------------------------------------------------- 世系图

void person_tree(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);

  const std::string mode = req->getParameter("mode").empty()
                               ? std::string("full")
                               : req->getParameter("mode");
  if (mode != "full" && mode != "five_generation") {
    cb(problem_response(400, 1001, "参数校验失败",
                        "mode 应为 full 或 five_generation", instance));
    return;
  }

  std::vector<FieldError> errors;
  int depth = 10;
  if (!parse_depth(req, &depth, &errors)) {
    cb(validation_problem(instance, errors, ""));
    return;
  }

  PersonService svc(*db);
  auto me = svc.get_person(id);
  if (!me) {
    cb(error_to_response(me.error(), instance));
    return;
  }

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }

  // five_generation：本人 + 上下各两代（五世一图的传统版式）
  const int effective_depth = mode == "five_generation" ? 2 : depth;

  // 节点集合：ego + 祖先链 + 子孙树，边由父子/婚姻关系推出
  std::vector<const Person*> nodes;
  nodes.push_back(&*me);
  for (int d = 1; d <= effective_depth; ++d) {
    std::vector<const Person*> next;
    for (const auto* p : nodes) {
      if (const auto* f = graph.father_of(p->id)) next.push_back(f);
      if (const auto* m = graph.mother_of(p->id)) next.push_back(m);
    }
    for (const auto* p : next) nodes.push_back(p);
  }
  auto down = lineage::descendants_tree(graph, id, effective_depth);
  if (down) {
    std::function<void(const lineage::TreeNode&)> walk =
        [&](const lineage::TreeNode& n) {
          if (n.person != nullptr) nodes.push_back(n.person);
          for (const auto& c : n.children) walk(c);
        };
    walk(*down);
  }
  std::sort(nodes.begin(), nodes.end());
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());

  nlohmann::json out;
  out["mode"] = mode;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto* p : nodes) arr.push_back(to_json(*p));
  out["nodes"] = std::move(arr);

  // 边：出生边（父/母 → 子）与婚姻边（配偶之间）。
  // 婚姻边在规范里 type=marriage，这里用 undirected 表示，
  // 客户端按 from/to 自行定向（同性情侣不受性别假设影响）。
  nlohmann::json edges = nlohmann::json::array();
  for (const auto* p : nodes) {
    if (p->father_id) {
      const auto* f = graph.find(*p->father_id);
      if (f != nullptr && std::binary_search(nodes.begin(), nodes.end(), f)) {
        edges.push_back({{"from", f->id}, {"to", p->id}, {"type", "birth"}});
      }
    }
    if (p->mother_id) {
      const auto* m = graph.find(*p->mother_id);
      if (m != nullptr && std::binary_search(nodes.begin(), nodes.end(), m)) {
        edges.push_back({{"from", m->id}, {"to", p->id}, {"type", "birth"}});
      }
    }
    for (const auto* s : graph.spouses_of(p->id)) {
      if (p->id < s->id &&
          std::binary_search(nodes.begin(), nodes.end(), s)) {
        edges.push_back({{"from", p->id}, {"to", s->id}, {"type", "marriage"}});
      }
    }
  }
  out["edges"] = std::move(edges);
  cb(json_ok(out, std::nullopt));
}

// ---------------------------------------------------------------- 五服

void person_mourning(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);

  const std::string to = req->getParameter("to");
  if (to.empty()) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少必填参数 to", instance));
    return;
  }
  std::string ruleset = req->getParameter("ruleset");
  if (ruleset.empty()) ruleset = "ming_qing";
  if (ruleset != "ming_qing" && ruleset != "ancient" && ruleset != "custom") {
    cb(problem_response(400, 1001, "参数校验失败",
                        "ruleset 应为 ming_qing / ancient / custom", instance));
    return;
  }

  PersonService svc(*db);
  auto me = svc.get_person(id);
  if (!me) {
    cb(error_to_response(me.error(), instance));
    return;
  }
  auto other = svc.get_person(to);
  if (!other) {
    cb(error_to_response(other.error(), instance));
    return;
  }

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }

  lineage::MourningQuery q;
  q.ego = id;
  q.alter = to;
  q.ruleset = ruleset;
  auto deg = lineage::compute_mourning_degree(graph, q);
  if (!deg) {
    cb(error_to_response(deg.error(), instance));
    return;
  }

  nlohmann::json out;
  out["garment"] = to_string(deg->grade);
  out["garment_name"] = deg->garment_name;
  out["duration_months"] = deg->period_months;
  out["period"] = deg->period;
  out["rule_id"] = deg->rule_id;   // 命中的规则标识（path 步骤序列）
  out["ruleset"] = deg->ruleset;
  out["basis"] = deg->basis;
  cb(json_ok(out, std::nullopt));
}

// ---------------------------------------------------------------- 称谓

void person_appellation(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = path_id(req);

  const std::string to = req->getParameter("to");
  if (to.empty()) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少必填参数 to", instance));
    return;
  }

  PersonService svc(*db);
  auto me = svc.get_person(id);
  if (!me) {
    cb(error_to_response(me.error(), instance));
    return;
  }
  auto other = svc.get_person(to);
  if (!other) {
    cb(error_to_response(other.error(), instance));
    return;
  }

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }

  lineage::AppellationQuery q;
  q.ego = id;
  q.alter = to;
  auto app = lineage::compute_appellation(graph, q);
  if (!app) {
    cb(error_to_response(app.error(), instance));
    return;
  }

  nlohmann::json out;
  out["standard"] = app->standard;
  out["colloquial"] = app->colloquial;
  out["formal"] = app->formal;
  out["elder"] = app->elder;
  out["path"] = app->path;   // 规则表字母序列（F.M.B+ …）
  out["basis"] = app->basis;
  cb(json_ok(out, std::nullopt));
}

}  // namespace genealogy::server
