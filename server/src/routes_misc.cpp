#include <genealogy/server/routes_misc.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <drogon/HttpTypes.h>
#include <drogon/HttpResponse.h>
#include <drogon/MultiPart.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/io/gedcom.hpp>
#include <genealogy/io/tree_render.hpp>
#include <genealogy/service/graph_service.hpp>
#include <genealogy/service/tree_service.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/document.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/document_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/place_repository.hpp>
#include <genealogy/repository/source_repository.hpp>
#include <genealogy/repository/bulk_load.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/server/auth.hpp>
#include <genealogy/server/audit.hpp>
#include <genealogy/server/idempotency.hpp>
#include <genealogy/server/routes_auth.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/validate/validation.hpp>

namespace genealogy::server {

namespace {

}  // namespace

// ================================================================ 检索

void search(const drogon::HttpRequestPtr& req, Callback&& cb) {
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

  const std::string q = req->getParameter("q");
  if (q.empty()) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少必填参数 q", instance));
    return;
  }
  // scope 三值（spec 已声明枚举）：person=人物、document=文档正文
  // （M7.4 trigram FTS，验收门「文档可按正文检索命中」）、all=两者合并。
  // scope=all 的 items 带 kind 判别字段（"person"/"document"，人物在前）；
  // 单一 scope 的 items 不加 kind（保持既有形状）。未知 scope 返回 400
  // 而非空列表——空列表会让调用方以为「查过了，没有」。
  const std::string scope = req->getParameter("scope").empty()
                                ? std::string("all")
                                : req->getParameter("scope");
  if (scope != "person" && scope != "document" && scope != "all") {
    cb(problem_response(400, 1001, "参数校验失败",
                        "scope 应为 person / document / all", instance));
    return;
  }

  const bool want_person = scope == "person" || scope == "all";
  const bool want_document = scope == "document" || scope == "all";

  std::vector<nlohmann::json> items;
  std::string next;
  bool more = false;

  if (want_person) {
    PersonService svc(*db);
    auto got = svc.search_persons(q, page.limit + 1);
    if (!got) {
      cb(error_to_response(got.error(), instance));
      return;
    }
    if (got->size() > static_cast<std::size_t>(page.limit)) {
      more = true;
      got->pop_back();
    }
    const bool viewer_raw = identity_of(req).raw_privacy;
    for (auto& p : *got) {
      nlohmann::json j = to_json(p);
      sanitize_person(j, viewer_raw);
      if (scope == "all") j["kind"] = "person";
      items.push_back(std::move(j));
      next = p.id;  // 游标口径：最后一个命中 id（scope=all 仅人物参与翻页）
    }
    if (scope == "person") {
      cb(json_ok(page_envelope(std::move(items), more ? next : "", more),
                 std::nullopt));
      return;
    }
    // scope=all：人物已占满一页时不再查文档
    if (!more) {
      DocumentRepository docs(*db);
      auto doc_hits = docs.search(q, page.limit + 1);
      if (!doc_hits) {
        cb(error_to_response(doc_hits.error(), instance));
        return;
      }
      if (doc_hits->size() > static_cast<std::size_t>(page.limit)) {
        more = true;
        doc_hits->pop_back();
      }
      for (auto& d : *doc_hits) {
        nlohmann::json j = to_json(d);
        j["kind"] = "document";
        items.push_back(std::move(j));
        next = d.id;
      }
    }
    cb(json_ok(page_envelope(std::move(items), more ? next : "", more),
               std::nullopt));
    return;
  }

  // scope=document
  DocumentRepository docs(*db);
  auto doc_hits = docs.search(q, page.limit + 1);
  if (!doc_hits) {
    cb(error_to_response(doc_hits.error(), instance));
    return;
  }
  if (doc_hits->size() > static_cast<std::size_t>(page.limit)) {
    more = true;
    doc_hits->pop_back();
  }
  for (auto& d : *doc_hits) {
    items.push_back(to_json(d));
    next = d.id;
  }
  cb(json_ok(page_envelope(std::move(items), more ? next : "", more),
             std::nullopt));
}

// ================================================================ 统计

void stats(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();

  GraphService graphs(*db);
  auto loaded = graphs.load_full();
  if (!loaded) {
    cb(error_to_response(loaded.error(), instance));
    return;
  }
  const lineage::PersonGraph graph = std::move(*loaded);

  EventRepository events(*db);
  EventFilter ef;
  ef.limit = 300000;
  auto evs = events.list(ef);
  if (!evs) {
    cb(error_to_response(evs.error(), instance));
    return;
  }

  std::size_t living = 0;
  std::map<int, int> by_generation;
  for (const auto& [id, p] : graph.nodes()) {
    if (p->is_living) ++living;
    if (p->generation_no) ++by_generation[*p->generation_no];
  }

  nlohmann::json dist = nlohmann::json::array();
  for (const auto& [gen, count] : by_generation) {
    dist.push_back({{"generation_no", gen}, {"count", count}});
  }

  // 无证据事实（M7.3）：主体级口径——person/event 各查 citation 0 条
  // 的行数（与 R16 同一判定，但这里是 SQL 直查，不走全量装载）。
  CitationRepository citations(*db);
  const long uncited =
      citations.count_subjects_without_citation(CitationSubjectType::Person) +
      citations.count_subjects_without_citation(CitationSubjectType::Event);

  nlohmann::json out;
  out["person_count"] = graph.size();
  out["event_count"] = evs->size();
  out["living_count"] = living;
  out["branch_count"] = 0;      // 支派统计按宗族拆分，M3 补 lineage 维度
  out["uncited_facts"] = uncited;
  out["generation_distribution"] = std::move(dist);
  cb(json_ok(out, std::nullopt));
}

// ================================================================ 校验

void validate_run(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();

  ValidationOptions opt;
  if (!req->body().empty()) {
    try {
      const auto body = nlohmann::json::parse(req->body());
      if (body.contains("rule_ids") && body["rule_ids"].is_array()) {
        for (const auto& r : body["rule_ids"]) {
          if (r.is_string()) opt.only_rule_ids.push_back(r.get<std::string>());
        }
      }
    } catch (const std::exception& e) {
      cb(problem_response(400, 1001, "参数校验失败",
                          std::string("请求体不是合法 JSON: ") + e.what(),
                          instance));
      return;
    }
  }

  GraphService graphs(*db);
  auto loaded = graphs.load_full();
  if (!loaded) {
    cb(error_to_response(loaded.error(), instance));
    return;
  }
  const lineage::PersonGraph graph = std::move(*loaded);
  EventRepository events(*db);
  EventFilter ef;
  ef.limit = 300000;
  auto evs = events.list(ef);
  if (!evs) {
    cb(error_to_response(evs.error(), instance));
    return;
  }
  // R16/R17 需要全量 citation：翻页载到尽（与 CLI io validate 同口径，
  // 游标 = 本页最后一条 id）。events 的单次大 limit 是 M1 容量假设
  // （ROADMAP 暂缓项「统一装载层」）；citation 是新域，直接用翻页。
  CitationRepository citations(*db);
  std::vector<Citation> citation_rows;
  std::optional<std::string> cite_cursor;
  for (;;) {
    CitationFilter cf;
    cf.limit = 1000;
    cf.cursor = cite_cursor;
    auto page = citations.list(cf);
    if (!page) {
      cb(error_to_response(page.error(), instance));
      return;
    }
    if (page->empty()) break;
    for (auto& c : *page) citation_rows.push_back(std::move(c));
    cite_cursor = citation_rows.back().id;
  }

  auto report = validate(graph, &*evs, &citation_rows, opt);
  if (!report) {
    cb(error_to_response(report.error(), instance));
    return;
  }

  nlohmann::json issues = nlohmann::json::array();
  for (const auto& i : report->issues) {
    issues.push_back({{"rule_id", i.rule_id},
                      {"severity", to_string(i.severity)},
                      {"entity_type", i.entity_type},
                      {"entity_id", i.entity_id},
                      {"message", i.message},
                      {"suggestion", i.suggestion}});
  }
  nlohmann::json out;
  out["issues"] = std::move(issues);
  out["summary"] = {{"error", report->error_count},
                    {"warning", report->warning_count},
                    {"info", report->info_count}};
  out["ruleset"] = report->ruleset;
  out["ruleset_version"] = report->ruleset_version;
  out["rules_run"] = report->rules_run;
  out["rules_total"] = report->rules_total;
  cb(json_ok(out, std::nullopt));
}

// ================================================================ 导入

void import_gedcom(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  // 规范：multipart/form-data，字段 file（二进制）+ lineage_id。
  // Drogon 的 MultiPartParser 只在 content-type 确实是 multipart 时有用；
  // 这里同时接受 application/json 的 {content, lineage_id} 形式，
  // 方便脚本化调用（不必构造 multipart）。
  std::string content;
  std::string lineage_id;
  const std::string ctype = req->getHeader("content-type");
  if (ctype.rfind("multipart/form-data", 0) == 0) {
    // 规范 5.2.1：multipart/form-data，字段 file（GEDCOM 文本）+
    // lineage_id。Drogon 的 MultiPartParser::parse 返回 int（0 = 成功），
    // 文件段走 getFiles()（按 getItemName 取字段名），普通表单字段走
    // getParameters()。
    drogon::MultiPartParser parser;
    if (parser.parse(req) != 0) {
      cb(problem_response(400, 1001, "参数校验失败",
                          "multipart 解析失败", instance));
      return;
    }
    for (const auto& f : parser.getFiles()) {
      if (f.getItemName() == "file") {
        content.assign(f.fileContent());   // string_view → string
      }
    }
    // 注：上传件不会落盘（drogon 的 MultiPartParser 全程内存持有，
    // 只有显式调 save() 才写盘），uploads/tmp/ 下的 00-FF 子目录只是
    // app.run() 预建的空桶——无需也不会积累任何待清理文件。
    const auto& params = parser.getParameters();
    if (const auto it = params.find("lineage_id"); it != params.end()) {
      lineage_id = it->second;
    }
    if (content.empty()) {
      cb(problem_response(400, 1001, "参数校验失败", "multipart 里没有 file 段",
                          instance));
      return;
    }
  } else {
    try {
      const auto body = nlohmann::json::parse(req->body());
      if (body.contains("content") && body["content"].is_string()) {
        content = body["content"].get<std::string>();
      }
      if (body.contains("lineage_id") && body["lineage_id"].is_string()) {
        lineage_id = body["lineage_id"].get<std::string>();
      }
    } catch (const std::exception& e) {
      cb(problem_response(400, 1001, "参数校验失败",
                          std::string("请求体不是合法 JSON: ") + e.what(),
                          instance));
      return;
    }
  }

  if (lineage_id.empty()) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少 lineage_id", instance));
    return;
  }
  const std::string idem_key = req->getHeader("Idempotency-Key");
  if (auto idem_hit = idempotency_replay(*db, idem_key)) {
    cb(idem_hit->response);
    return;
  }

  auto doc = io::parse_gedcom(content);
  if (!doc) {
    cb(error_to_response(doc.error(), instance));
    return;
  }
  io::ImportOptions opt;
  opt.lineage_id = lineage_id;
  auto mapped = io::import_gedcom(*doc, opt);
  if (!mapped) {
    cb(error_to_response(mapped.error(), instance));
    return;
  }

  // 落库走 BulkLoader + Transaction（与 CLI 的 db seed 同一条快路径）：
  // 导入是批处理，逐条走用例层时每条 INSERT 一次 fsync，1 万条要分钟级。
  // 整个导入一个事务，中途失败整体回滚——不留半套谱。
  // 领域不变量里与外键相关的由数据库兜底；父母先于子女由
  // order_by_dependency 保证（GEDCOM 里子记录可以排在父记录前）。
  io::ImportResult data = std::move(*mapped);
  int created = 0;
  {
    Transaction tx(*db);
    if (tx.error()) {
      cb(problem_response(500, 9001, "内部错误",
                          "开启导入事务失败: " + tx.error()->to_string(),
                          instance));
      return;
    }
    BulkLoader loader(*db);
    std::string fail;
    for (const auto& pl : data.places) {
      if (auto e = loader.add_place(pl); !e) { fail = e.error().to_string(); break; }
      ++created;
    }
    if (fail.empty()) {
      for (const auto& p : io::order_by_dependency(data.persons)) {
        if (auto e = loader.add_person(p); !e) { fail = e.error().to_string(); break; }
        ++created;
      }
    }
    if (fail.empty()) {
      for (const auto& u : data.unions) {
        if (auto e = loader.add_union(u); !e) { fail = e.error().to_string(); break; }
        ++created;
      }
    }
    if (fail.empty()) {
      for (const auto& ev : data.events) {
        if (auto e = loader.add_event(ev); !e) { fail = e.error().to_string(); break; }
        ++created;
      }
    }
    // 出处链（M7.5）：来源先于引用（citation.source_id 外键）
    if (fail.empty()) {
      for (const auto& s : data.sources) {
        if (auto e = loader.add_source(s); !e) { fail = e.error().to_string(); break; }
        ++created;
      }
    }
    if (fail.empty()) {
      for (const auto& c : data.citations) {
        if (auto e = loader.add_citation(c); !e) { fail = e.error().to_string(); break; }
        ++created;
      }
    }
    if (!fail.empty() || !tx.commit()) {
      cb(problem_response(500, 9001, "内部错误",
                          "导入落库失败（已回滚，库中无残留）: " +
                              (fail.empty() ? std::string("提交失败") : fail),
                          instance));
      return;
    }
  }

  // M8.5：导入是一次大批量写入——审计记摘要（逐实体 diff 会让
  // audit_log 爆炸，总量在 imported 里、损失在 warnings 里可回溯）
  audit_write(*db, req, "import", "gedcom", "insert",
              {{"created", created}});
  nlohmann::json warnings = nlohmann::json::array();
  int skipped = 0;
  for (const auto& l : data.losses) {
    if (l.severity == io::LossSeverity::Warning) {
      warnings.push_back(l.xref + " " + l.tag + " — " + l.reason);
      ++skipped;
    }
  }
  nlohmann::json out;
  out["imported"] = created;
  out["skipped"] = skipped;
  out["warnings"] = std::move(warnings);
  idempotency_store(*db, idem_key, 200, out.dump());
  cb(json_ok(out, std::nullopt));
}

// ================================================================ 审计

// GET /audit?entity_type=&entity_id=&limit=&cursor= —— 变更史回溯
// （M8.5 验收门：任一人物的变更史可回溯）。仅 steward：diff 含原始
// 数据，viewer/editor 可读即绕过 M8.3 脱敏——审计是族尊职能。
void audit_query(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const auto identity = identity_of(req);
  if (identity.role != auth::Role::Steward) {
    cb(problem_response(403, 4001, "禁止访问",
                        "审计查询仅限 steward（当前角色 " +
                            std::string(auth::to_string(identity.role)) + "）",
                        instance));
    return;
  }
  Page page;
  std::vector<FieldError> errors;
  page = parse_page(req, &errors);
  if (!errors.empty()) {
    cb(validation_problem(instance, errors, "分页参数不合法"));
    return;
  }
  AuditQuery q;
  q.entity_type = req->getParameter("entity_type");
  q.entity_id = req->getParameter("entity_id");
  q.cursor = page.cursor;
  q.limit = page.limit;
  auto rows = audit_list(*db, q);
  if (!rows) {
    cb(error_to_response(rows.error(), instance));
    return;
  }
  const bool more = rows->size() > static_cast<std::size_t>(page.limit);
  if (more) rows->pop_back();
  nlohmann::json items = nlohmann::json::array();
  for (auto& r : *rows) {
    items.push_back({{"id", r.id},
                     {"entity_type", r.entity_type},
                     {"entity_id", r.entity_id},
                     {"action", r.action},
                     {"actor", r.actor},
                     {"at", r.at},
                     {"diff", r.diff}});
  }
  std::string next;
  if (more && !rows->empty()) next = rows->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
}

// ================================================================ 导出

void export_gedcom(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string lineage = req->getParameter("lineage_id");
  if (lineage.empty()) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少必填参数 lineage_id",
                        instance));
    return;
  }

  PersonRepository persons(*db);
  PersonFilter pf;
  pf.limit = 200000;
  pf.lineage_id = lineage;
  auto rows = persons.list(pf);
  if (!rows) {
    cb(error_to_response(rows.error(), instance));
    return;
  }

  // 结合与事件：一次查全再按「是否涉及导出集」过滤。
  // 曾经的写法是按人物逐个查（N+1）+ 线性去重，1 万人物要 57 秒。
  // person 列表按 id 排序（ORDER BY id），故可直接二分。
  std::vector<std::string> ids;
  ids.reserve(rows->size());
  for (const auto& p : *rows) ids.push_back(p.id);

  UnionRepository unions(*db);
  UnionFilter uf;
  uf.limit = 800000;
  auto urows = unions.list(uf);
  if (!urows) {
    cb(error_to_response(urows.error(), instance));
    return;
  }
  std::vector<Union> union_rows;
  for (auto& u : *urows) {
    for (const auto& m : u.members) {
      if (std::binary_search(ids.begin(), ids.end(), m.person_id)) {
        union_rows.push_back(std::move(u));
        break;
      }
    }
  }

  EventRepository events(*db);
  EventFilter ef;
  ef.limit = 800000;
  auto evs = events.list(ef);
  if (!evs) {
    cb(error_to_response(evs.error(), instance));
    return;
  }
  std::vector<Event> event_rows;
  for (auto& e : *evs) {
    if (std::binary_search(ids.begin(), ids.end(), e.subject_id)) {
      event_rows.push_back(std::move(e));
    }
  }

  PlaceRepository places(*db);
  PlaceFilter plf;
  plf.limit = 100000;
  auto place_rows = places.list(plf);
  if (!place_rows) {
    cb(error_to_response(place_rows.error(), instance));
    return;
  }

  // 出处链（M7.5）：来源全量 + 引用全量（citation 按 subject_id 与
  // 导出集的匹配由 export_gedcom 内部记账——主体不在集内的引用记
  // loss 而不是静默丢）。
  SourceRepository sources(*db);
  SourceFilter sf;
  sf.limit = 100000;
  auto source_rows = sources.list(sf);
  if (!source_rows) {
    cb(error_to_response(source_rows.error(), instance));
    return;
  }
  CitationRepository cites(*db);
  std::vector<Citation> citation_rows;
  std::optional<std::string> cite_cursor;
  for (;;) {
    CitationFilter cf;
    cf.limit = 1000;
    cf.cursor = cite_cursor;
    auto page = cites.list(cf);
    if (!page) {
      cb(error_to_response(page.error(), instance));
      return;
    }
    if (page->empty()) break;
    for (auto& c : *page) citation_rows.push_back(std::move(c));
    cite_cursor = citation_rows.back().id;
  }

  io::ExportOptions eo;
  eo.persons = &*rows;
  eo.unions = &union_rows;
  eo.events = &event_rows;
  eo.places = &*place_rows;
  eo.sources = &*source_rows;
  eo.citations = &citation_rows;
  // M8.3：导出同口径脱敏——raw 令牌（本地）完整导出，其余按在世者
  // privacy_level 屏蔽（ GedCOM 导出含在世者时即视为对外披露面）
  eo.respect_privacy = !identity_of(req).raw_privacy;
  // 导出侧损失台账：与导入同口径逐条记账（此前未传，fallback 直接
  // 丢弃——谱师看不到「库里有但没导出」的东西，与纪律相悖）。GEDCOM
  // 文本响应无结构化 losses 通道：数量走 X-Loss-Count 响应头。
  std::vector<io::MappingLoss> export_losses;
  eo.losses = &export_losses;
  const std::string text = io::export_gedcom(eo);

  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(text);
  resp->setContentTypeString("text/plain; charset=utf-8");
  if (!export_losses.empty()) {
    resp->addHeader("X-Loss-Count", std::to_string(export_losses.size()));
  }
  cb(resp);
}

void export_tree(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  const std::string instance = req->path();
  const std::string id = req->getParameter("person_id");
  if (id.empty()) {
    cb(problem_response(400, 1001, "参数校验失败", "缺少必填参数 person_id",
                        instance));
    return;
  }
  const std::string format = req->getParameter("format").empty()
                                 ? std::string("dot")
                                 : req->getParameter("format");
  if (format != "dot" && format != "mermaid" && format != "svg") {
    cb(problem_response(400, 1001, "参数校验失败",
                        "format 应为 dot / mermaid / svg", instance));
    return;
  }

  // 组树 + 渲染全在 core（TreeService + io::render_tree），server 不
  // 手拼字符串——CLI 的 io export tree 与这里共用同一份实现（ADR-1）。
  // svg 需 graphviz 管线，属展示层，这里明确 501 而不是假装支持。
  if (format == "svg") {
    cb(problem_response(501, 9001, "尚未实现",
                        "svg 需接 graphviz，可先取 dot", instance));
    return;
  }
  TreeService trees(*db);
  auto text = trees.export_tree(
      id, format == "mermaid" ? io::TreeFormat::Mermaid : io::TreeFormat::Dot, 10);
  if (!text) {
    cb(error_to_response(text.error(), instance));
    return;
  }

  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(*text);
  resp->setContentTypeString("text/plain; charset=utf-8");
  cb(resp);
}

// ================================================================ 运维

void health(const drogon::HttpRequestPtr& req, Callback&& cb) {
  std::string why;
  auto* db = db_from(req, &why);
  if (db == nullptr) {
    cb(problem_response(500, 9001, "内部错误", why, req->path()));
    return;
  }
  // 探活顺便验证库可用：跑一条最便宜的查询
  PersonRepository persons(*db);
  PersonFilter f;
  f.limit = 1;
  auto rows = persons.list(f);
  nlohmann::json out;
  out["status"] = rows ? "ok" : "degraded";
  out["version"] = "1.0.0";
  cb(json_ok(out, std::nullopt));
}

void openapi_doc(const drogon::HttpRequestPtr& req, Callback&& cb) {
  // openapi.yaml 是客户端代码生成的唯一事实源；服务端原样托管。
  // 文件由 CMake 拷到可执行文件旁边（见 server/CMakeLists.txt）。
  // 查找顺序：可执行文件目录 → cwd → 仓库相对路径（开发树直跑用）。
  // 只用 cwd 是不行的——服务常由 systemd/脚本从别处拉起，
  // 一换工作目录就 500，而这种故障在现场极难复现。
  //
  // 候选路径一律走 fs::path：Windows 的窄字符 std::ifstream 以 ANSI
  // 代码页解释路径，中文安装目录必炸；fs::path 在 MSVC 下以宽字符
  // 开文件。旧写法 std::string(w.begin(), w.end()) 是逐元素截断而非
  // 编码转换，非 ASCII 路径直接变乱码（CI 四跑的 C4244 揭发）。
  std::vector<std::filesystem::path> candidates;
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    const std::filesystem::path exe(buf, buf + n);
    candidates.push_back(exe.parent_path() / "openapi.yaml");
  }
#endif
  candidates.emplace_back("openapi.yaml");
  candidates.emplace_back("shared/openapi/openapi.yaml");

  std::ifstream f;
  for (const auto& path : candidates) {
    f.open(path, std::ios::binary);
    if (f) break;
    f.clear();
  }
  if (!f) {
    cb(problem_response(500, 9001, "内部错误",
                        "找不到 openapi.yaml（应随可执行文件一同分发）",
                        req->path()));
    return;
  }
  std::ostringstream os;
  os << f.rdbuf();
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(os.str());
  resp->setContentTypeString("application/yaml; charset=utf-8");
  cb(resp);
}

}  // namespace genealogy::server
