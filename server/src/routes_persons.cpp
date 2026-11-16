#include <genealogy/server/routes_persons.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <drogon/HttpTypes.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/model/migration.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/person_name.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/migration_repository.hpp>
#include <genealogy/repository/person_name_repository.hpp>
#include <genealogy/service/person_name_service.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/event_bus.hpp>
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

// expand 查询参数是 OpenAPI 的 form/explode:false 数组：
// 客户端发 ?expand=events,migrations 单值逗号分隔。切分 + 去空白。
std::vector<std::string> split_csv_expand(const std::string& value) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t comma = value.find(',', start);
    const std::size_t end = comma == std::string::npos ? value.size() : comma;
    std::string item = value.substr(start, end - start);
    while (!item.empty() && item.back() == ' ') item.pop_back();
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    if (!item.empty()) out.push_back(std::move(item));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
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
  GENEALOGY_PUBLISH_CHANGE("person.created", "person", made->id, made->rev);
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

  // expand=events,unions,migrations（form/explode:false → 单值逗号分隔）。
  // 人物卡/详情页靠它拿子资源——此前 spec 声明了五种展开而实现为零，
  // 客户端请求被静默忽略（声明跑在实现前面的审计项）。未展开的子资源
  // 不出现，响应结构与此前完全兼容。子资源按 keyset 分页拉全
  // （此前单页 200 硬截断、超限静默缺失）；仓储出错向上传播为 500，
  // 不伪装成「没有子资源」。
  nlohmann::json body = to_json(*got);
  bool want_events = false;
  bool want_unions = false;
  bool want_migrations = false;
  bool want_names = false;
  for (const auto& item : split_csv_expand(req->getParameter("expand"))) {
    if (item == "events") want_events = true;
    else if (item == "unions") want_unions = true;
    else if (item == "migrations") want_migrations = true;
    else if (item == "names") want_names = true;
  }
  // 单主体子资源的安全上限：正常人不会有一万条事件/迁徙；真到了
  // 上限宁可截断也不无限翻页（与 200000 全量装载的口径取舍一致）。
  constexpr std::size_t kExpandCap = 10000;
  auto fetch_all = [&](auto& repo, auto filter, auto* out) -> Result<bool> {
    // 返回 bool 表示是否触达安全上限
    std::vector<std::remove_reference_t<decltype(*out->begin())>> all;
    for (int guard = 0; guard < 100; ++guard) {
      auto page = repo.list(filter);
      if (!page) return page.error();
      const bool last = page->size() < static_cast<std::size_t>(filter.limit);
      for (auto& item : *page) all.push_back(std::move(item));
      if (last || all.size() >= kExpandCap) break;
      filter.cursor = all.back().id;
    }
    *out = std::move(all);
    return all.size() >= kExpandCap;
  };
  if (want_events) {
    EventRepository events(*db);
    EventFilter ef;
    ef.subject_type = EventSubjectType::Person;
    ef.subject_id = got->id;
    ef.limit = 200;
    std::vector<Event> evs;
    auto fetched = fetch_all(events, ef, &evs);
    if (!fetched) {
      cb(error_to_response(fetched.error(), instance));
      return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : evs) arr.push_back(to_json(e));
    body["events"] = arr;
  }
  if (want_unions) {
    UnionRepository unions(*db);
    UnionFilter uf;
    uf.person_id = got->id;
    uf.limit = 200;
    std::vector<Union> us;
    auto fetched = fetch_all(unions, uf, &us);
    if (!fetched) {
      cb(error_to_response(fetched.error(), instance));
      return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& u : us) arr.push_back(to_json(u));
    body["unions"] = arr;
  }
  if (want_migrations) {
    MigrationRepository migrations(*db);
    MigrationFilter mf;
    mf.subject_type = MigrationSubjectType::Person;
    mf.subject_id = got->id;
    mf.limit = 200;
    std::vector<Migration> ms;
    auto fetched = fetch_all(migrations, mf, &ms);
    if (!fetched) {
      cb(error_to_response(fetched.error(), instance));
      return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : ms) arr.push_back(to_json(m));
    body["migrations"] = arr;
  }
  if (want_names) {
    PersonNameRepository names(*db);
    auto ns = names.list(got->id);
    if (!ns) {
      cb(error_to_response(ns.error(), instance));
      return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : *ns) arr.push_back(to_json(n));
    body["names"] = arr;
  }
  cb(json_ok(std::move(body), got->rev));
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
  // 亲子关系：字符串改指、null 解除（dto 层解析，见 parse_person_update）
  if (body.contains("father_id")) updated.father_id = d.father_id;
  if (body.contains("mother_id")) updated.mother_id = d.mother_id;
  if (body.contains("note")) updated.note = d.note;

  updated.id = current->id;
  updated.rev = *want;   // 仓储层据此做乐观锁比对

  auto saved = svc.update_person(updated);
  if (!saved) {
    cb(error_to_response(saved.error(), instance));
    return;
  }
  cb(json_ok(to_json(*saved), saved->rev));
  GENEALOGY_PUBLISH_CHANGE("person.updated", "person", saved->id, saved->rev);
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
  GENEALOGY_PUBLISH_CHANGE("person.deleted", "person", path_id(req),
                           std::nullopt);
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

// ------------------------------------------------------- 名号（M7.1）

void person_names_list(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string person_id = path_id(req);
  // 人物存在性先查：404 而不是「空列表」（空会让调用方以为查过了没有）
  PersonRepository persons(*db);
  auto got = persons.get(person_id);
  if (!got) {
    cb(error_to_response(got.error(), req->path()));
    return;
  }
  PersonNameRepository names(*db);
  auto ns = names.list(person_id);
  if (!ns) {
    cb(error_to_response(ns.error(), req->path()));
    return;
  }
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& n : *ns) arr.push_back(to_json(n));
  cb(json_ok(page_envelope(std::move(arr), std::string{}, false),
             std::nullopt));
}

void person_names_create(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string person_id = path_id(req);
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req->body());
  } catch (const std::exception& e) {
    cb(problem_response(400, 1001, "参数校验失败",
                        std::string("请求体不是合法 JSON: ") + e.what(),
                        instance));
    return;
  }

  PersonNameService svc(*db);
  PersonName draft;
  draft.person_id = person_id;
  if (const auto it = body.find("name"); it != body.end() && it->is_string()) {
    draft.name = it->get<std::string>();
  }
  if (const auto it = body.find("name_type");
      it != body.end() && it->is_string()) {
    auto t = person_name_type_from_string(it->get<std::string>());
    if (!t) {
      cb(validation_problem(instance,
                            {{"name_type", "应为 genealogy/infant/school/"
                                          "renamed/posthumous"}},
                            "名号类型不合法"));
      return;
    }
    draft.type = *t;
  }
  // 日期接受 GenealogyDate JSON（ISO 年份/完整日期均可）。缺失或 null
  // = 不设；非对象或解析失败走 400——名号起止是事实，猜不得。
  const auto opt_date = [&](const char* k) -> Result<GenealogyDate> {
    const auto it = body.find(k);
    if (it == body.end() || it->is_null()) return GenealogyDate{};
    if (!it->is_object()) {
      return invalid_argument(std::string(k) +
                              " 应为日期对象（如 {\"gregorian\":\"1901\"}）");
    }
    return GenealogyDate::from_json(*it);
  };
  {
    auto d1 = opt_date("start_date");
    if (!d1) {
      cb(validation_problem(instance, {{"start_date", d1.error().message()}},
                            "日期不合法"));
      return;
    }
    if (d1->gregorian.has_value() || d1->lunar.has_value()) {
      draft.start_date = *d1;
    }
    auto d2 = opt_date("end_date");
    if (!d2) {
      cb(validation_problem(instance, {{"end_date", d2.error().message()}},
                            "日期不合法"));
      return;
    }
    if (d2->gregorian.has_value() || d2->lunar.has_value()) {
      draft.end_date = *d2;
    }
  }
  if (const auto it = body.find("is_primary");
      it != body.end() && it->is_boolean()) {
    draft.is_primary = it->get<bool>();
  }
  if (const auto it = body.find("note"); it != body.end() && it->is_string()) {
    draft.note = it->get<std::string>();
  }

  auto made = svc.create_person_name(draft);
  if (!made) {
    cb(error_to_response(made.error(), instance));
    return;
  }
  // 名号变更入 SSE（人物详情随名单变化）
  GENEALOGY_PUBLISH_CHANGE("person.updated", "person", person_id, 0);
  cb(json_created(to_json(*made)));
}

void person_name_delete(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const auto& params = req->getRoutingParameters();
  if (params.size() < 2) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少名号 id",
                        req->path()));
    return;
  }
  PersonNameService svc(*db);
  auto removed = svc.remove_person_name(params[1]);
  if (!removed) {
    cb(error_to_response(removed.error(), req->path()));
    return;
  }
  GENEALOGY_PUBLISH_CHANGE("person.updated", "person", params[0], 0);
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setStatusCode(drogon::k204NoContent);
  cb(resp);
}

}  // namespace genealogy::server
