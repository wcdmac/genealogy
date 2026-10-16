#include <genealogy/server/routes_misc.hpp>

#include <algorithm>
#include <cstdint>
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
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/place_repository.hpp>
#include <genealogy/repository/bulk_load.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/validate/validation.hpp>

namespace genealogy::server {

namespace {

// 全库快照：检索/统计/校验共用。与 routes_persons 里那份同口径
// （M1 规模全量装载；更大规模改按需子图，见设计文档 4.4.1）。
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
  // 零拷贝：图按值接收后把两个结果向量移进去自持（见 graph.hpp 的
  // 生命周期说明）。曾经的写法是 build(*rows, *urows)——图里存的是
  // 指向这两个局部 Result 的裸指针，函数一返回全图悬空，
  // 症状是 validate 时 SIGSEGV，而 stats/search 靠未覆写内存侥幸正常。
  return lineage::PersonGraph::build(std::move(*rows), std::move(*urows));
}

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
  // scope 目前只实现 person：document/source 的模型在 M3 才建表，
  // 这里明确拒绝而不是返回空列表——空列表会让调用方以为「查过了，没有」。
  const std::string scope = req->getParameter("scope").empty()
                                ? std::string("all")
                                : req->getParameter("scope");
  if (scope != "person" && scope != "all") {
    cb(problem_response(501, 9001, "尚未实现",
                        "scope=" + scope + " 的检索在 M3 补模型后开放",
                        instance));
    return;
  }

  PersonService svc(*db);
  auto got = svc.search_persons(q, page.limit + 1);
  if (!got) {
    cb(error_to_response(got.error(), instance));
    return;
  }
  const bool more = got->size() > static_cast<std::size_t>(page.limit);
  if (more) got->pop_back();
  std::vector<nlohmann::json> items;
  for (const auto& p : *got) items.push_back(to_json(p));
  std::string next;
  if (more && !got->empty()) next = got->back().id;
  cb(json_ok(page_envelope(std::move(items), next, more), std::nullopt));
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

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }

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

  nlohmann::json out;
  out["person_count"] = graph.size();
  out["event_count"] = evs->size();
  out["living_count"] = living;
  out["branch_count"] = 0;      // 支派统计按宗族拆分，M3 补 lineage 维度
  out["uncited_facts"] = 0;     // 无 Source/Citation 模型，恒 0（M3）
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

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }
  EventRepository events(*db);
  EventFilter ef;
  ef.limit = 300000;
  auto evs = events.list(ef);
  if (!evs) {
    cb(error_to_response(evs.error(), instance));
    return;
  }

  auto report = validate(graph, &*evs, opt);
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
    if (!fail.empty() || !tx.commit()) {
      cb(problem_response(500, 9001, "内部错误",
                          "导入落库失败（已回滚，库中无残留）: " +
                              (fail.empty() ? std::string("提交失败") : fail),
                          instance));
      return;
    }
  }

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
  cb(json_ok(out, std::nullopt));
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

  io::ExportOptions eo;
  eo.persons = &*rows;
  eo.unions = &union_rows;
  eo.events = &event_rows;
  eo.places = &*place_rows;
  const std::string text = io::export_gedcom(eo);

  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(text);
  resp->setContentTypeString("text/plain; charset=utf-8");
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

  const lineage::PersonGraph graph = load_graph(*db, &why);
  if (!why.empty()) {
    cb(problem_response(500, 9001, "内部错误", why, instance));
    return;
  }
  auto tree = lineage::descendants_tree(graph, id, 10);
  if (!tree) {
    cb(error_to_response(tree.error(), instance));
    return;
  }

  // 节点标识一律用人物 id，标签才是姓名。用姓名当 key 是错的：
  // 同名同姓在同族里太常见（行辈 + 派语字决定了重名率），
  // 那样导出图会把不同的人并成一个点，且 dot/mermaid 语法都会坏。
  std::ostringstream os;
  if (format == "mermaid") {
    os << "graph TD\n";
    std::function<void(const lineage::TreeNode&, const std::string&)> emit =
        [&](const lineage::TreeNode& n, const std::string& parent) {
          const std::string self =
              n.person ? n.person->id : std::string("root");
          const std::string label =
              n.person ? n.person->family_name + n.person->given_name : "?";
          if (!parent.empty()) {
            os << "  " << parent << " --> \"" << self << "\"\n";
          }
          os << "  \"" << self << "\"[\"" << label << "\"]\n";
          for (const auto& c : n.children) emit(c, self);
        };
    emit(*tree, "");
  } else {
    // dot 与 svg 都先出 DOT（svg 由 graphviz 渲染，这里不引其依赖）
    os << "digraph genealogy {\n  rankdir=TB;\n";
    std::function<void(const lineage::TreeNode&, const std::string&)> emit =
        [&](const lineage::TreeNode& n, const std::string& parent) {
          const std::string self =
              n.person ? n.person->id : std::string("root");
          const std::string label =
              n.person ? n.person->family_name + n.person->given_name : "?";
          if (!parent.empty()) {
            os << "  \"" << parent << "\" -> \"" << self << "\"\n";
          }
          os << "  \"" << self << "\" [label=\"" << label << "\"]\n";
          for (const auto& c : n.children) emit(c, self);
        };
    emit(*tree, "");
    os << "}\n";
    if (format == "svg") {
      cb(problem_response(501, 9001, "尚未实现",
                          "svg 需接 graphviz，M3 补；可先取 dot", instance));
      return;
    }
  }

  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(os.str());
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
  std::vector<std::string> candidates;
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    std::wstring w(buf, n);
    const auto slash = w.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
      const std::wstring dir = w.substr(0, slash);
      std::string utf8(dir.begin(), dir.end());
      candidates.push_back(utf8 + "/openapi.yaml");
    }
  }
#endif
  candidates.push_back("openapi.yaml");
  candidates.push_back("shared/openapi/openapi.yaml");

  std::ifstream f;
  std::string used;
  for (const auto& path : candidates) {
    f.open(path, std::ios::binary);
    if (f) {
      used = path;
      break;
    }
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
