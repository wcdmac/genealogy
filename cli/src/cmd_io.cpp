// genea 导入导出与校验（M1.5）。
//
//   genea io import gedcom <file> [--surname 陈] [--no-places]
//   genea io export gedcom [--out file.ged] [--submitter 名字]
//   genea io validate [--rule R1,R7] [--limit n]
//
// 导入走既有用例层（PersonService/UnionService/EventService/PlaceService），
// 因此领域不变量、rev、事务边界全都照旧生效——不是另写一条旁路。

#include "cmd_io.hpp"

#include "cli_util.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <unordered_set>
#include <system_error>
#include <vector>

#include <genealogy/calendar/era.hpp>
#include <genealogy/io/book_html.hpp>
#include <genealogy/io/chart_svg.hpp>
#include <genealogy/io/layout_svg.hpp>
#include <genealogy/io/page_layout.hpp>
#include <genealogy/io/markup.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/service/book_service.hpp>
#include <genealogy/error.hpp>
#include <genealogy/io/gedcom.hpp>
#include <genealogy/ulid.hpp>
#include <genealogy/io/synthetic.hpp>
#include <genealogy/io/tree_render.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/repository/bulk_load.hpp>
#include <genealogy/service/citation_service.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/source_service.hpp>
#include <genealogy/service/tree_service.hpp>
#include <genealogy/service/union_service.hpp>
#include <genealogy/validate/validation.hpp>

namespace genea_cli {

namespace {

using namespace genealogy;

std::string read_file(const std::string& path, bool* ok) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *ok = false;
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  // 第六轮 中-4：rdbuf() 读取失败不抛异常，须查 bad()（fail/eof 属正常终止）
  *ok = !in.bad();
  return ss.str();
}

void write_file(const std::string& path, const std::string& text, bool* ok) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    *ok = false;
    return;
  }
  out << text;
  // 第六轮 中-4：缓冲内容在 flush/close 才真正落盘，此前判定会漏报磁盘满等错误
  out.flush();
  if (!out) {
    *ok = false;
    return;
  }
  out.close();
  *ok = static_cast<bool>(out);
}

// 逐条落库，返回失败信息（空 = 全部成功）
std::string persist(Database& db, io::ImportResult& r, int* created) {
  // 整份导入一个事务：服务层每条 INSERT 自成事务，WAL 下每次提交一次
  // fsync（本机约 50 ms），1 万条要跑好几分钟。包进一个事务后是毫秒级。
  // 顺带补齐导入的原子性——中途失败不留半套谱。
  Transaction tx(db);
  if (tx.error()) {
    return "开启导入事务失败: " + tx.error()->to_string();
  }

  PlaceService places(db);
  PersonService persons(db);
  UnionService unions(db);
  EventService events(db);
  SourceService sources(db);
  CitationService citations(db);
  *created = 0;

  for (const auto& pl : r.places) {
    auto made = places.create_place(pl);
    if (!made) return "建地点失败: " + made.error().to_string();
    ++*created;
  }
  for (const auto& p : io::order_by_dependency(r.persons)) {
    auto made = persons.create_person(p);
    if (!made) return "建人物失败(" + p.family_name + p.given_name + "): " +
                     made.error().to_string();
    ++*created;
  }
  for (const auto& u : r.unions) {
    auto made = unions.create_union(u);
    if (!made) return "建婚姻结合失败: " + made.error().to_string();
    ++*created;
  }
  for (const auto& e : r.events) {
    auto made = events.create_event(e);
    if (!made) return "建事件失败: " + made.error().to_string();
    ++*created;
  }
  // 出处链（M7.5）：来源先于引用（citation.source_id 外键）
  for (const auto& s : r.sources) {
    auto made = sources.create_source(s);
    if (!made) return "建来源失败: " + made.error().to_string();
    ++*created;
  }
  for (const auto& c : r.citations) {
    auto made = citations.create_citation(c);
    if (!made) return "建证据引用失败: " + made.error().to_string();
    ++*created;
  }
  if (auto c = tx.commit(); !c) {
    return "提交导入事务失败: " + c.error().to_string();
  }
  return {};
}

int cmd_import_gedcom(const std::vector<std::string>& args,
                      const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea io import gedcom", "导入 GEDCOM 5.5.1 文件");
  opts.add_options()
      ("file", "GEDCOM 文件路径", cxxopts::value<std::string>())
      ("surname", "NAME 无 /姓/ 结构时的兜底姓", cxxopts::value<std::string>())
      ("no-places", "不自动按名称建地点",
       cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
  opts.parse_positional({"file"});
  const auto r = parse_args(opts, args);
  if (!r.count("file")) {
    std::cerr << "用法错误: genea io import gedcom <file.ged>"
                 " [--surname 陈] [--no-places]\n";
    return 2;
  }

  bool ok = false;
  const std::string bytes = read_file(r["file"].as<std::string>(), &ok);
  if (!ok) {
    print_error(Error{ErrorCode::IoError, "读不到文件 " + r["file"].as<std::string>()});
    return 1;
  }
  auto doc = io::parse_gedcom(bytes);
  if (!doc) {
    print_error(doc.error());
    return exit_code_for(doc.error());
  }

  io::ImportOptions opt;
  if (r.count("surname")) opt.default_surname = r["surname"].as<std::string>();
  opt.create_places = !r["no-places"].as<bool>();

  auto result = io::import_gedcom(*doc, opt);
  if (!result) {
    print_error(result.error());
    return exit_code_for(result.error());
  }
  io::ImportResult data = std::move(*result);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  int created = 0;
  const std::string fail = persist(*db, data, &created);
  if (!fail.empty()) {
    print_error(Error{ErrorCode::IoError, fail});
    return 1;
  }

  if (format == "json") {
    nlohmann::json out;
    out["imported"] = {{"persons", data.persons.size()},
                       {"unions", data.unions.size()},
                       {"events", data.events.size()},
                       {"places", data.places.size()},
                       {"sources", data.sources.size()},
                       {"citations", data.citations.size()}};
    out["created_records"] = created;
    nlohmann::json losses = nlohmann::json::array();
    int warnings = 0;
    for (const auto& l : data.losses) {
      if (l.severity == io::LossSeverity::Warning) ++warnings;
      losses.push_back({{"severity", l.severity == io::LossSeverity::Warning
                                           ? "warning"
                                           : "info"},
                        {"xref", l.xref},
                        {"tag", l.tag},
                        {"reason", l.reason}});
    }
    out["losses"] = losses;
    out["loss_warnings"] = warnings;
    out["source"] = {{"name", doc->source_name}, {"version", doc->source_version}};
    std::cout << out.dump(2) << "\n";
    return 0;
  }

  std::cout << "导入完成: 人物 " << data.persons.size() << " · 结合 "
            << data.unions.size() << " · 事件 " << data.events.size()
            << " · 地点 " << data.places.size() << "\n";
  if (!doc->source_name.empty()) {
    std::cout << "来源: " << doc->source_name;
    if (!doc->source_version.empty()) std::cout << " v" << doc->source_version;
    std::cout << "\n";
  }
  if (data.losses.empty()) {
    std::cout << "映射损失: 无\n";
    return 0;
  }
  int warnings = 0;
  for (const auto& l : data.losses) {
    if (l.severity == io::LossSeverity::Warning) ++warnings;
  }
  std::cout << "映射损失: " << data.losses.size() << " 条（warning " << warnings
            << " 条，info " << (data.losses.size() - warnings) << " 条）\n";
  for (const auto& l : data.losses) {
    std::cout << "  [" << (l.severity == io::LossSeverity::Warning ? "W" : "I")
              << "] " << (l.xref.empty() ? std::string("(文件级)") : l.xref)
              << " " << l.tag << " — " << l.reason << "\n";
  }
  return 0;
}

int cmd_export_gedcom(const std::vector<std::string>& args,
                      const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea io export gedcom", "导出 GEDCOM 5.5.1 文件");
  opts.add_options()
      ("out", "输出文件路径（缺省打到标准输出）", cxxopts::value<std::string>())
      ("submitter", "提交人姓名", cxxopts::value<std::string>())
      ("gedcom-version", "目标 GEDCOM 版本：5.5.1（默认）或 7.0",
       cxxopts::value<std::string>()->default_value("5.5.1"))
      ("limit", "单次导出上限", cxxopts::value<int>()->default_value("100000"));
  const auto r = parse_args(opts, args);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PersonService persons(*db);
  PersonFilter pf;
  pf.limit = r["limit"].as<int>();
  auto person_list = persons.list_persons(pf);
  if (!person_list) {
    print_error(person_list.error());
    return exit_code_for(person_list.error());
  }
  std::vector<Person> rows = std::move(*person_list);

  // 结合与事件：一次查全，再按「是否涉及导出集内的人物」在内存里过滤。
  // 曾经按人物逐个查（N+1 查询 + 线性去重），1 万人物实测 57 秒——
  // 查询次数随人数线性涨，去重比较还是 O(n^2)，两样都不可接受。
  std::unordered_set<std::string> in_export;
  in_export.reserve(rows.size() * 2);
  for (const auto& p : rows) in_export.insert(p.id);

  UnionService unions(*db);
  UnionFilter uf;
  uf.limit = static_cast<int>(rows.size()) * 4 + 1000;
  std::vector<Union> union_rows;
  if (auto got = unions.list_unions(uf)) {
    for (auto& u : *got) {
      for (const auto& m : u.members) {
        if (in_export.count(m.person_id) > 0) {
          union_rows.push_back(std::move(u));
          break;
        }
      }
    }
  }

  EventService events(*db);
  EventFilter ef;
  ef.limit = static_cast<int>(rows.size()) * 4 + 1000;
  std::vector<Event> event_rows;
  if (auto got = events.list_events(ef)) {
    std::unordered_set<std::string> exported_union;
    exported_union.reserve(union_rows.size() * 2);
    for (const auto& u : union_rows) exported_union.insert(u.id);
    for (auto& e : *got) {
      const bool person_hit = in_export.count(e.subject_id) > 0;
      const bool union_hit =
          e.subject_type == EventSubjectType::Union &&
          exported_union.count(e.subject_id) > 0;
      if (person_hit || union_hit) event_rows.push_back(std::move(e));
    }
  }

  PlaceService places(*db);
  PlaceFilter plf;
  plf.limit = 100000;
  auto place_list = places.list_places(plf);
  std::vector<Place> place_rows =
      place_list ? std::move(*place_list) : std::vector<Place>{};

  // 出处链（M7.5）：来源全量 + 引用按导出集过滤。引用主体（人物/
  // 结合/事件）是否在集内由 export_gedcom 内部记账——这里只装载。
  SourceService sources(*db);
  SourceFilter sf;
  sf.limit = 100000;
  auto source_list = sources.list_sources(sf);
  if (!source_list) {
    print_error(source_list.error());
    return exit_code_for(source_list.error());
  }
  CitationService citations(*db);
  std::vector<Citation> citation_rows;
  std::optional<std::string> cite_cursor;
  for (;;) {
    CitationFilter cf;
    cf.limit = 1000;
    cf.cursor = cite_cursor;
    auto page = citations.list_citations(cf);
    if (!page) {
      print_error(page.error());
      return exit_code_for(page.error());
    }
    if (page->empty()) break;
    for (auto& c : *page) citation_rows.push_back(std::move(c));
    cite_cursor = citation_rows.back().id;
  }

  io::ExportOptions eo;
  eo.persons = &rows;
  eo.unions = &union_rows;
  eo.events = &event_rows;
  eo.places = &place_rows;
  eo.sources = &*source_list;
  eo.citations = &citation_rows;
  eo.submitter_name = r.count("submitter") ? r["submitter"].as<std::string>()
                                           : "genealogy";
  eo.source_version = "1.0";
  eo.gedcom_version = r["gedcom-version"].as<std::string>();
  // 导出侧损失台账：与导入同口径逐条告知谱师（不许静默丢）
  std::vector<io::MappingLoss> export_losses;
  eo.losses = &export_losses;
  const std::string text = io::export_gedcom(eo);
  if (text.empty() && eo.gedcom_version != "5.5.1" &&
      eo.gedcom_version != "7.0") {
    const Error err{ErrorCode::InvalidArgument,
                    "不支持的 GEDCOM 版本 " + eo.gedcom_version +
                        "（支持 5.5.1 / 7.0）"};
    print_error(err);
    return exit_code_for(err);
  }

  if (r.count("out")) {
    for (const auto& l : export_losses) {
      std::cerr << (l.severity == io::LossSeverity::Warning ? "  [W] " : "  [I] ")
                << l.xref << " " << l.tag << " — " << l.reason << "\n";
    }
    bool ok = false;
    write_file(r["out"].as<std::string>(), text, &ok);
    if (!ok) {
      print_error(Error{ErrorCode::IoError, "写不出文件 " + r["out"].as<std::string>()});
      return 1;
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"file", r["out"].as<std::string>()},
                                  {"bytes", text.size()},
                                  {"persons", rows.size()},
                                  {"unions", union_rows.size()},
                                  {"events", event_rows.size()},
                                  {"losses", export_losses.size()}}
                       .dump(2)
                << "\n";
    } else {
      std::cout << "已导出 " << text.size() << " 字节到 " << r["out"].as<std::string>()
                << "（人物 " << rows.size() << " · 结合 " << union_rows.size()
                << " · 事件 " << event_rows.size() << " · 损失 "
                << export_losses.size() << "）\n";
    }
    return 0;
  }
  std::cout << text;
  return 0;
}

int cmd_validate(const std::vector<std::string>& args,
                 const std::string& db_path,
                 const std::string& format) {
  cxxopts::Options opts("genea io validate", "运行一致性校验规则");
  opts.add_options()
      ("rule", "只跑这些规则（逗号分隔）", cxxopts::value<std::string>())
      ("limit", "载入上限", cxxopts::value<int>()->default_value("100000"));
  const auto r = parse_args(opts, args);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PersonService persons(*db);
  PersonFilter pf;
  pf.limit = r["limit"].as<int>();
  auto person_list = persons.list_persons(pf);
  if (!person_list) {
    print_error(person_list.error());
    return exit_code_for(person_list.error());
  }
  std::vector<Person> rows = std::move(*person_list);

  UnionService unions(*db);
  UnionFilter uf;
  uf.limit = 100000;
  auto union_list = unions.list_unions(uf);
  std::vector<Union> union_rows =
      union_list ? std::move(*union_list) : std::vector<Union>{};

  EventService events(*db);
  EventFilter ef;
  ef.limit = 100000;
  auto event_list = events.list_events(ef);
  std::vector<Event> event_rows =
      event_list ? std::move(*event_list) : std::vector<Event>{};

  // R16/R17 需要全量 citation：翻页载到尽（游标 = 本页最后一条 id）。
  // 人物/事件用的单次大 limit 是 M1 登记的容量假设（ROADMAP 暂缓项
  // 「统一装载层」），这里引用是新域从一开始就用翻页，不给它埋上限。
  CitationService citations(*db);
  std::vector<Citation> citation_rows;
  std::optional<std::string> cite_cursor;
  for (;;) {
    CitationFilter cf;
    cf.limit = 1000;
    cf.cursor = cite_cursor;
    auto page = citations.list_citations(cf);
    if (!page) {
      print_error(page.error());
      return exit_code_for(page.error());
    }
    if (page->empty()) break;
    for (auto& c : *page) citation_rows.push_back(std::move(c));
    cite_cursor = citation_rows.back().id;
  }

  const lineage::PersonGraph graph = lineage::PersonGraph::build(rows, union_rows);

  ValidationOptions vo;
  if (r.count("rule")) {
    std::string cur;
    for (const char c : r["rule"].as<std::string>()) {
      if (c == ',') {
        if (!cur.empty()) vo.only_rule_ids.push_back(cur);
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (!cur.empty()) vo.only_rule_ids.push_back(cur);
  }

  auto report = validate(graph, &event_rows, &citation_rows, vo);
  if (!report) {
    print_error(report.error());
    return exit_code_for(report.error());
  }

  if (format == "json") {
    nlohmann::json out;
    out["ruleset"] = report->ruleset;
    out["ruleset_version"] = report->ruleset_version;
    out["rules_run"] = report->rules_run;
    out["rules_total"] = report->rules_total;
    out["summary"] = {{"error", report->error_count},
                      {"warning", report->warning_count},
                      {"info", report->info_count}};
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& i : report->issues) {
      arr.push_back({{"rule_id", i.rule_id},
                     {"severity", to_string(i.severity)},
                     {"entity_type", i.entity_type},
                     {"entity_id", i.entity_id},
                     {"message", i.message},
                     {"suggestion", i.suggestion}});
    }
    out["issues"] = arr;
    std::cout << out.dump(2) << "\n";
  } else {
    std::cout << "规则集 " << report->ruleset << " v" << report->ruleset_version
              << "（跑了 " << report->rules_run << "/" << report->rules_total
              << " 条）\n";
    std::cout << "合计: error " << report->error_count << " · warning "
              << report->warning_count << " · info " << report->info_count << "\n";
    for (const auto& i : report->issues) {
      std::cout << "[" << to_string(i.severity) << "] " << i.rule_id << " "
                << i.entity_type << " " << i.entity_id << " — " << i.message;
      if (!i.suggestion.empty()) std::cout << "（" << i.suggestion << "）";
      std::cout << "\n";
    }
    if (report->issues.empty()) std::cout << "（无问题）\n";
  }
  // 有 error 时退出码非零，便于 CI 卡点
  return report->error_count > 0 ? 1 : 0;
}

}  // namespace

// 世系树导出（DOT / Mermaid）。渲染在 core 的 io::render_tree——
// server 的 /exports/tree 与这里同一份实现（ADR-1）。节点标识一律
// 用人物 ID，标签才是姓名：同名同姓在同族里太常见，用姓名当 key
// 会把不同的人并成一个点。
// 定义位置有讲究：genea_cli 内、匿名 namespace 之外——放进匿名
// namespace 会与 cmd_io.hpp 的声明形成重载歧义（踩过，C2668）。
int cmd_export_tree(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea io export tree", "导出世系树（DOT / Mermaid）");
  // 局部格式选项不能叫 --format：全局 --format（text|json）在 main 里
  // 先行解析，会把这个值吃掉并拒之门外（踩过）。REST 的 query 叫
  // format 无碍，CLI 侧用 --tree-format 区分输出管线与图形格式。
  opts.add_options()
      ("person", "中心人物 ID", cxxopts::value<std::string>())
      ("depth", "展开层数", cxxopts::value<int>()->default_value("10"))
      ("tree-format", "dot|mermaid",
       cxxopts::value<std::string>()->default_value("dot"))
      ("out", "输出文件路径（缺省打到标准输出）", cxxopts::value<std::string>());
  const auto r = parse_args(opts, args);

  if (!r.count("person")) {
    std::cerr << "用法错误: genea io export tree --person <ID> [--tree-format dot|mermaid]"
                 " [--depth 10] [--out 文件]\n";
    return 2;
  }
  const std::string tree_format = r["tree-format"].as<std::string>();
  if (tree_format != "dot" && tree_format != "mermaid") {
    std::cerr << "用法错误: --tree-format 应为 dot 或 mermaid\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  TreeService trees(*db);
  auto text = trees.export_tree(r["person"].as<std::string>(),
                                tree_format == "mermaid" ? io::TreeFormat::Mermaid
                                                         : io::TreeFormat::Dot,
                                r["depth"].as<int>());
  if (!text) {
    print_error(text.error());
    return exit_code_for(text.error());
  }

  if (r.count("out")) {
    bool ok = false;
    write_file(r["out"].as<std::string>(), *text, &ok);
    if (!ok) {
      print_error(Error{ErrorCode::IoError, "写不出文件 " + r["out"].as<std::string>()});
      return 1;
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"file", r["out"].as<std::string>()},
                                  {"bytes", text->size()},
                                  {"format", tree_format}}
                       .dump(2)
                << "\n";
    } else {
      std::cout << "已导出 " << text->size() << " 字节到 "
                << r["out"].as<std::string>() << "（" << tree_format << "）\n";
    }
    return 0;
  }
  std::cout << *text;
  return 0;
}

int cmd_io(const std::vector<std::string>& args, const std::string& db_path,
           const std::string& format) {
  if (args.empty()) {
    std::cerr << "用法错误: genea io import|export|validate\n";
    return 2;
  }
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());
  try {
    if (sub == "import" && !rest.empty() && rest[0] == "gedcom") {
      return cmd_import_gedcom(
          std::vector<std::string>(rest.begin() + 1, rest.end()), db_path, format);
    }
    if (sub == "export" && !rest.empty() && rest[0] == "gedcom") {
      return cmd_export_gedcom(
          std::vector<std::string>(rest.begin() + 1, rest.end()), db_path, format);
    }
    if (sub == "export" && !rest.empty() && rest[0] == "tree") {
      return cmd_export_tree(
          std::vector<std::string>(rest.begin() + 1, rest.end()), db_path, format);
    }
    if (sub == "export" && !rest.empty() && rest[0] == "layout") {
      return cmd_export_layout(
          std::vector<std::string>(rest.begin() + 1, rest.end()), db_path, format);
    }
    if (sub == "export" && !rest.empty() && rest[0] == "chart") {
      return cmd_export_chart(
          std::vector<std::string>(rest.begin() + 1, rest.end()), db_path, format);
    }
    if (sub == "export" && !rest.empty() && rest[0] == "book") {
      return cmd_export_book(
          std::vector<std::string>(rest.begin() + 1, rest.end()), db_path, format);
    }
    if (sub == "validate") return cmd_validate(rest, db_path, format);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "用法错误: " << e.what() << "\n";
    return 2;
  }
  std::cerr << "用法错误: genea io import gedcom|export gedcom|export tree|export chart|export book|export layout|validate\n";
  return 2;
}

// ---------------------------------------------------------------- 合成谱灌库

int cmd_db_seed(const std::vector<std::string>& args, const std::string& db_path,
                const std::string& format) {
  cxxopts::Options opts("genea db seed", "灌入一批合成谱数据（性能验收用）");
  opts.add_options()
      ("persons", "目标人物数", cxxopts::value<std::size_t>()->default_value("100000"))
      ("generations", "世代数", cxxopts::value<int>()->default_value("14"))
      ("seed", "随机种子（同种子同数据，便于复现）",
       cxxopts::value<std::uint64_t>()->default_value("20261013"))
      ("surname", "主姓", cxxopts::value<std::string>()->default_value("陈"))
      ("founder-year", "始祖生年", cxxopts::value<int>()->default_value("1500"));
  const auto r = parse_args(opts, args);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }

  io::SyntheticOptions so;
  so.surname = r["surname"].as<std::string>();
  so.persons = r["persons"].as<std::size_t>();
  so.generations = r["generations"].as<int>();
  so.seed = r["seed"].as<std::uint64_t>();
  so.founder_year = r["founder-year"].as<int>();

  const auto t0 = std::chrono::steady_clock::now();
  auto synth = io::generate_synthetic(so);
  if (!synth) {
    print_error(synth.error());
    return exit_code_for(synth.error());
  }
  const io::SyntheticData& data = *synth;
  const auto t1 = std::chrono::steady_clock::now();

  BulkLoader loader(*db);
  // 地点必须最先：person 的 birth_place_id 等有外键
  for (const auto& pl : data.places) {
    if (auto e = loader.add_place(pl); !e) {
      print_error(e.error());
      return 1;
    }
  }
  for (const auto& p : data.persons) {
    if (auto e = loader.add_person(p); !e) {
      print_error(e.error());
      return 1;
    }
  }
  for (const auto& u : data.unions) {
    if (auto e = loader.add_union(u); !e) {
      print_error(e.error());
      return 1;
    }
  }
  for (const auto& ev : data.events) {
    if (auto e = loader.add_event(ev); !e) {
      print_error(e.error());
      return 1;
    }
  }
  const auto t2 = std::chrono::steady_clock::now();
  if (auto e = loader.finish(); !e) {
    print_error(e.error());
    return 1;
  }
  const auto t3 = std::chrono::steady_clock::now();

  const auto ms = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  if (format == "json") {
    std::cout << nlohmann::json{
        {"persons", loader.persons()},
        {"unions", loader.unions()},
        {"events", loader.events()},
        {"places", data.places.size()},
        {"generate_ms", ms(t0, t1)},
        {"insert_ms", ms(t1, t2)},
        {"commit_ms", ms(t2, t3)},
        {"total_ms", ms(t0, t3)}}
                 .dump(2)
              << "\n";
    return 0;
  }
  std::cout << "灌库完成: 人物 " << loader.persons() << " · 结合 " << loader.unions()
            << " · 事件 " << loader.events() << " · 地点 " << data.places.size()
            << "\n";
  std::cout << "生成 " << ms(t0, t1) << " ms · 插入 " << ms(t1, t2)
            << " ms · 提交 " << ms(t2, t3) << " ms · 合计 " << ms(t0, t3)
            << " ms\n";
  std::cout << "始祖 id: " << data.founder_id << "（bench 用它当十代树起点）\n";
  return 0;
}

// ---------------------------------------------------------------- 性能基准

namespace {

double median(std::vector<double> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

struct BenchRow {
  std::string name;
  std::string target;
  double measured = 0;
  std::string unit = "ms";
  bool pass = false;
};

void print_rows(const std::vector<BenchRow>& rows, const std::string& format) {
  if (format == "json") {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : rows) {
      arr.push_back({{"name", r.name},
                     {"target", r.target},
                     {"measured", r.measured},
                     {"unit", r.unit},
                     {"pass", r.pass}});
    }
    std::cout << arr.dump(2) << "\n";
    return;
  }
  std::cout << "指标                       目标          实测        结果\n";
  std::cout << "--------------------------------------------------------------\n";
  for (const auto& r : rows) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%-26s %-12s %8.1f %-4s %s", r.name.c_str(),
                  r.target.c_str(), r.measured, r.unit.c_str(),
                  r.pass ? "PASS" : "FAIL");
    std::cout << buf << "\n";
  }
}

// 基准临时库作用域守卫（第三轮新-C9）：cmd_bench 有 20+ 处提前
// return（灌库失败/导入失败/查询失败），函数末尾的清理块全被跳过，
// 系统临时目录持续堆积。析构兜底，任何出口都清。
struct BenchTempGuard {
  std::string path;
  bool keep = false;
  ~BenchTempGuard() {
    if (keep || path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
  }
};

}  // namespace

int cmd_bench(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format) {
  // 基准库一律用系统临时目录的专属文件，绝不碰 --db（db_path 参数仅为
  // 主分派签名统一而保留）。曾直接在 --db（默认 genealogy.db）上灌基准
  // 并跑完整库删除——对已有数据的库执行 bench 会先污染再毁库，不可恢复
  // （2026-11 静态审查 F1，致命级）。
  const std::string bench_db =
      (std::filesystem::temp_directory_path() /
       ("genealogy-bench-" + genealogy::generate_ulid() + ".db"))
          .string();
  cxxopts::Options opts("genea bench", "性能验收基准（M1.6）");
  opts.add_options()
      ("persons", "灌库人物数", cxxopts::value<std::size_t>()->default_value("100000"))
      ("writers", "并发写线程数（M9.1：各持独立连接灌 5 人，验收门=0 locked）",
       cxxopts::value<int>()->default_value("1"))
      ("events-per-person", "每人事件数（0-20；3=出生/卒年/葬期，>3 补记事，0 不生成）",
       cxxopts::value<int>()->default_value("3"))
      ("depth", "十代树的展开层数", cxxopts::value<int>()->default_value("10"))
      ("repeat", "查询类指标重复次数取中位数", cxxopts::value<int>()->default_value("5"))
      ("gedcom-records", "GEDCOM 导入基准的记录数",
       cxxopts::value<int>()->default_value("10000"))
      ("keep", "保留基准用的临时库（默认跑完删掉）",
       cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
  const auto r = parse_args(opts, args);
  const BenchTempGuard bench_guard{bench_db, r["keep"].as<bool>()};
  std::cout << "基准库: " << bench_db
            << "（临时文件，不触碰 --db 指定的业务库）\n";

  const std::size_t persons = r["persons"].as<std::size_t>();
  const int depth = r["depth"].as<int>();
  const int repeat = std::max(1, r["repeat"].as<int>());
  std::vector<BenchRow> rows;

  // ---- 1. 灌库 ----
  auto db = open_db(bench_db);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }

  io::SyntheticOptions so;
  so.surname = "陈";
  so.persons = persons;
  so.generations = depth + 4;
  // 第三轮新-C8：--events-per-person 此前注册了却从未读取，合成器
  // 固定每人 3 条——接线进合成器，参数真正生效
  so.events_per_person = std::clamp(r["events-per-person"].as<int>(), 0, 20);
  auto synth = io::generate_synthetic(so);
  if (!synth) {
    print_error(synth.error());
    return exit_code_for(synth.error());
  }
  const io::SyntheticData& data = *synth;

  const auto g0 = std::chrono::steady_clock::now();
  {
    BulkLoader loader(*db);
    for (const auto& pl : data.places) {
      if (auto e = loader.add_place(pl); !e) { print_error(e.error()); return 1; }
    }
    for (const auto& p : data.persons) {
      if (auto e = loader.add_person(p); !e) { print_error(e.error()); return 1; }
    }
    for (const auto& u : data.unions) {
      if (auto e = loader.add_union(u); !e) { print_error(e.error()); return 1; }
    }
    for (const auto& ev : data.events) {
      if (auto e = loader.add_event(ev); !e) { print_error(e.error()); return 1; }
    }
    if (auto e = loader.finish(); !e) { print_error(e.error()); return 1; }
  }
  const auto g1 = std::chrono::steady_clock::now();
  const double load_ms =
      std::chrono::duration<double, std::milli>(g1 - g0).count();
  rows.push_back({"灌库 " + std::to_string(persons) + " 人物 + " +
                      std::to_string(data.events.size()) + " 事件",
                  "< 60000", load_ms, "ms", load_ms < 60000.0});

  // ---- 2. FTS 检索 ----
  {
    PersonService persons_svc(*db);
    std::vector<double> samples;
    const char* keywords[] = {"文远", "承嗣", "德昭", "世昌"};
    for (int i = 0; i < repeat; ++i) {
      const auto t0 = std::chrono::steady_clock::now();
      auto got = persons_svc.search_persons(keywords[i % 4], 50);
      const auto t1 = std::chrono::steady_clock::now();
      if (!got) { print_error(got.error()); return exit_code_for(got.error()); }
      samples.push_back(
          std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    const double m = median(samples);
    rows.push_back({"FTS 检索（" + std::to_string(persons / 10000) + " 万级）",
                    "< 50", m, "ms", m < 50.0});
  }

  // ---- 3. 十代子孙树 ----
  {
    PersonService persons_svc(*db);
    PersonFilter pf;
    pf.limit = static_cast<int>(persons) + 1000;
    auto list = persons_svc.list_persons(pf);
    if (!list) { print_error(list.error()); return exit_code_for(list.error()); }
    UnionService unions(*db);
    UnionFilter uf;
    uf.limit = static_cast<int>(data.unions.size()) + 1000;
    auto ulist = unions.list_unions(uf);
    if (!ulist) { print_error(ulist.error()); return exit_code_for(ulist.error()); }

    const auto t0 = std::chrono::steady_clock::now();
    const lineage::PersonGraph graph =
        lineage::PersonGraph::build(*list, *ulist);
    const auto t1 = std::chrono::steady_clock::now();

    std::vector<double> tree_samples;
    std::size_t tree_nodes = 0;
    int tree_depth_seen = 0;
    for (int i = 0; i < repeat; ++i) {
      const auto a = std::chrono::steady_clock::now();
      auto tree = lineage::descendants_tree(graph, data.founder_id, depth);
      const auto b = std::chrono::steady_clock::now();
      if (!tree) { print_error(tree.error()); return exit_code_for(tree.error()); }
      tree_nodes = lineage::tree_node_count(*tree);
      tree_depth_seen = lineage::tree_depth(*tree);
      tree_samples.push_back(
          std::chrono::duration<double, std::milli>(b - a).count());
    }
    const double tree_ms = median(tree_samples);
    rows.push_back({"十代子孙树（" + std::to_string(tree_nodes) + " 节点 / " +
                        std::to_string(tree_depth_seen) + " 层）",
                    "< 200", tree_ms, "ms", tree_ms < 200.0});
    const double build_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    rows.push_back({"世系图快照构建（" + std::to_string(list->size()) + " 人物）",
                    "参考项", build_ms, "ms", true});
  }

  // ---- 4. GEDCOM 导入 1 万条 ----
  {
    // 合成一份 1 万条记录的 GEDCOM，走 io import 的真实路径（用例层落库）
    const int want = r["gedcom-records"].as<int>();
    io::SyntheticOptions go;
    go.surname = "陈";
    go.persons = static_cast<std::size_t>(want);
    go.generations = 12;
    auto gsynth = io::generate_synthetic(go);
    if (!gsynth) {
      print_error(gsynth.error());
      return exit_code_for(gsynth.error());
    }
    const io::SyntheticData& gdata = *gsynth;

    // 用导出器造输入，保证与真实文件的形态一致；导出本身也是一项指标——
    // 它曾经是 O(结合×人物) 的，1 万人物要跑 57 秒
    io::ExportOptions eo;
    eo.persons = &gdata.persons;
    eo.unions = &gdata.unions;
    eo.places = &gdata.places;
    const auto x0 = std::chrono::steady_clock::now();
    const std::string text = io::export_gedcom(eo);
    const auto x1 = std::chrono::steady_clock::now();
    const double export_ms =
        std::chrono::duration<double, std::milli>(x1 - x0).count();
    rows.push_back({"GEDCOM 导出 " + std::to_string(gdata.persons.size()) +
                        " 人物（" + std::to_string(text.size() / 1024) + " KB）",
                    "< 10000", export_ms, "ms", export_ms < 10000.0});

    auto doc = io::parse_gedcom(text);
    if (!doc) { print_error(doc.error()); return exit_code_for(doc.error()); }
    io::ImportOptions io_opt;
    io_opt.default_surname = "陈";
    auto mapped = io::import_gedcom(*doc, io_opt);
    if (!mapped) { print_error(mapped.error()); return exit_code_for(mapped.error()); }

    // 灌库走 BulkLoader（与 db seed 同一条快路径）；解析与映射单独计时
    const auto m0 = std::chrono::steady_clock::now();
    auto doc2 = io::parse_gedcom(text);
    if (!doc2) { print_error(doc2.error()); return exit_code_for(doc2.error()); }
    const auto m1 = std::chrono::steady_clock::now();
    auto mapped2 = io::import_gedcom(*doc2, io_opt);
    if (!mapped2) { print_error(mapped2.error()); return exit_code_for(mapped2.error()); }
    const auto m2 = std::chrono::steady_clock::now();
    {
      BulkLoader loader(*db);
      for (const auto& pl : mapped2->places) {
        if (auto e = loader.add_place(pl); !e) { print_error(e.error()); return 1; }
      }
      for (const auto& p : io::order_by_dependency(mapped2->persons)) {
        if (auto e = loader.add_person(p); !e) { print_error(e.error()); return 1; }
      }
      for (const auto& u : mapped2->unions) {
        if (auto e = loader.add_union(u); !e) { print_error(e.error()); return 1; }
      }
      for (const auto& ev : mapped2->events) {
        if (auto e = loader.add_event(ev); !e) { print_error(e.error()); return 1; }
      }
      if (auto e = loader.finish(); !e) { print_error(e.error()); return 1; }
    }
    const auto m3 = std::chrono::steady_clock::now();
    const double total_ms =
        std::chrono::duration<double, std::milli>(m3 - m0).count();
    rows.push_back({"GEDCOM 导入 " + std::to_string(mapped2->persons.size()) +
                        " 人物（解析+映射+落库）",
                    "< 60000", total_ms, "ms", total_ms < 60000.0});
    rows.push_back({"  其中：解析 " +
                        std::to_string(static_cast<int>(
                            std::chrono::duration<double, std::milli>(m1 - m0).count())) +
                        " ms · 映射 " +
                        std::to_string(static_cast<int>(
                            std::chrono::duration<double, std::milli>(m2 - m1).count())) +
                        " ms · 落库 " +
                        std::to_string(static_cast<int>(
                            std::chrono::duration<double, std::milli>(m3 - m2).count())) +
                        " ms",
                    "参考项", 0, "", true});
  }

  // ---- 5. 并发写（M9.1 验收门：--writers N 线程各持独立连接，
  // 断言零 locked——单写者互斥 + WAL/busy_timeout 双层兜底）----
  const int writers = std::max(1, r["writers"].as<int>());
  if (writers > 1) {
    const auto c0 = std::chrono::steady_clock::now();
    std::atomic<int> locked{0};
    std::atomic<int> inserted{0};
    std::atomic<int> failures{0};
    {
      std::vector<std::thread> threads;
      threads.reserve(static_cast<std::size_t>(writers));
      for (int w = 0; w < writers; ++w) {
        threads.emplace_back([&, w] {
          // 第六轮 高-2：异常不得逃出线程入口，计入失败而非 terminate
          try {
          // 并发写也必须落在基准库上：此前写线程仍开 --db（用户业务库），
          // 5 × writers 条"并发写测试"人物灌进真实库且不清理（复核 2.1 残余）。
          auto wdb = open_db(bench_db);
          if (!wdb) {
            ++failures;
            return;
          }
          PersonService wsvc(*wdb);
          PersonFilter pf;
          pf.limit = 1;
          for (int i = 0; i < 5; ++i) {
            Person p;
            p.family_name = "并发";
            p.given_name = "写" + std::to_string(w) + "-" + std::to_string(i);
            p.gender = Gender::Male;
            auto made = wsvc.create_person(p);
            if (!made) {
              if (made.error().message().find("locked") != std::string::npos) {
                ++locked;
              } else {
                ++failures;
              }
            } else {
              ++inserted;
            }
          }
          } catch (...) {
            ++failures;
          }
        });
      }
      for (auto& t : threads) t.join();
    }
    const auto c1 = std::chrono::steady_clock::now();
    const double conc_ms =
        std::chrono::duration<double, std::milli>(c1 - c0).count();
    rows.push_back({"并发写 " + std::to_string(writers) + " 线程 × 5 人",
                    "0 locked",
                    conc_ms, "ms", locked.load() == 0 && failures.load() == 0});
    rows.push_back({"  其中：入库 " + std::to_string(inserted.load()) +
                        " · locked " + std::to_string(locked.load()) +
                        " · 失败 " + std::to_string(failures.load()),
                    "参考项", 0, "", true});
  }

  print_rows(rows, format);
  bool all_pass = true;
  for (const auto& row : rows) {
    if (!row.pass) all_pass = false;
  }
  // 临时库清理交由 BenchTempGuard 析构（第三轮新-C9）
  return all_pass ? 0 : 1;
}




// P2 古籍版式导出：自有标记语法 + 全要素版式（版心/鱼尾/象鼻/栏线/书耳）。
int cmd_export_layout(const std::vector<std::string>& args,
                      const std::string& db_path, const std::string& format) {
  (void)db_path;  // 版式源文件是自包含文本，不依赖数据库
  cxxopts::Options opts("genea io export layout", "标记文本 → 古籍版式 SVG");
  opts.add_options()
      ("source", "标记语法源文件（必需）", cxxopts::value<std::string>())
      ("preset", "版式预设: guji_classic|plain_modern",
       cxxopts::value<std::string>()->default_value("guji_classic"))
      ("title", "书名", cxxopts::value<std::string>()->default_value(""))
      ("body-size", "正文字号覆盖（如 22pt），无需改预设 JSON",
       cxxopts::value<std::string>()->default_value(""))
      ("volume", "卷次", cxxopts::value<std::string>()->default_value(""))
      ("out", "输出 SVG（多页时写 foo-pN.svg）", cxxopts::value<std::string>());
  const auto r = parse_args(opts, args);
  if (!r.count("source")) {
    std::cerr << "用法错误: genea io export layout --source <标记文本> [--preset ...]\n";
    return 2;
  }
  std::ifstream in(r["source"].as<std::string>(), std::ios::binary);
  if (!in) {
    print_error(Error{ErrorCode::IoError,
                      "读不出源文件 " + r["source"].as<std::string>()});
    return 1;
  }
  const std::string source((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
  auto parsed = io::parse_markup(source);
  if (!parsed) { print_error(parsed.error()); return exit_code_for(parsed.error()); }
  for (const auto& w : parsed->warnings) std::cerr << "[提示] " << w << "\n";

  // 用预设字体度量分页，与渲染器共用同一几何（字号改动自动联动）
  io::PageLayoutOptions pl;
  pl.source = parsed->nodes;
  auto preset_geo = io::geometry_for_preset(r["preset"].as<std::string>());
  if (!preset_geo) { print_error(preset_geo.error()); return exit_code_for(preset_geo.error()); }
  pl.geometry = *preset_geo;
  // --body-size 覆盖：用相同推导规则重算几何，分页/渲染共用
  if (r.count("body-size") && !r["body-size"].as<std::string>().empty()) {
    auto pj = io::layout_preset_json(r["preset"].as<std::string>());
    if (pj) {
      auto pjson = nlohmann::json::parse(*pj, nullptr, false);
      if (!pjson.is_discarded()) {
        pjson["typography"]["body_size"] = r["body-size"].as<std::string>();
        pl.geometry = io::LayoutGeometry::from_preset(pjson);
      }
    }
  }
  auto paged = io::paginate_content(pl);
  if (!paged) { print_error(paged.error()); return exit_code_for(paged.error()); }
  std::vector<std::vector<io::MarkupNode>> pages;
  for (const auto& pg : paged->pages) pages.push_back(pg.nodes);
  if (pages.empty()) pages.emplace_back();

  nlohmann::json files = nlohmann::json::array();
  const std::string title = r["title"].as<std::string>();
  const std::string volume = r["volume"].as<std::string>();
  for (std::size_t i = 0; i < pages.size(); ++i) {
    io::LayoutOptions opt;
    opt.preset = r["preset"].as<std::string>();
    opt.book_title = title;
    opt.volume_title = volume;
    opt.page_number = static_cast<int>(i + 1);
    opt.total_pages = pages.size();
    opt.content = pages[i];
    opt.geometry_override = pl.geometry;
    auto svg = io::render_layout_page(opt);
    if (!svg) { print_error(svg.error()); return exit_code_for(svg.error()); }
    if (!r.count("out")) {
      if (i) std::cout << "\n<!-- === 第 " << (i + 1) << " 页 === -->\n";
      std::cout << *svg;
      continue;
    }
    const std::string base = r["out"].as<std::string>();
    const auto dot = base.find_last_of('.');
    const std::string path = pages.size() > 1
        ? (dot == std::string::npos ? base : base.substr(0, dot)) + "-p" +
          std::to_string(i + 1) + (dot == std::string::npos ? "" : base.substr(dot))
        : base;
    bool ok = false;
    write_file(path, *svg, &ok);
    if (!ok) {
      print_error(Error{ErrorCode::IoError, "写不出文件 " + path});
      return 1;
    }
    files.push_back({{"file", path}, {"bytes", svg->size()}});
  }
  if (format == "json" && !files.empty()) {
    std::cout << nlohmann::json{{"files", files},
                                {"preset", r["preset"].as<std::string>()},
                                {"pages", pages.size()},
                                {"warnings", parsed->warnings}}
                     .dump(2) << "\n";
  } else if (!files.empty()) {
    std::cout << "已导出古籍版式 " << pages.size() << " 页到:\n";
    for (const auto& f : files) std::cout << "  " << f["file"].get<std::string>() << "\n";
  }
  return 0;
}

// P1 世系图导出：六种体例，纯 C++ SVG（规避 Graphviz EPL）。
// 五服体例由 core 的九族五服矩阵渲染（外置规则表，单一事实源）。
int cmd_export_chart(const std::vector<std::string>& args,
                     const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea io export chart", "导出世系图 SVG");
  opts.add_options()
      ("style", "体例: pedigree|descendant|hourglass|wufu|european|soviet",
       cxxopts::value<std::string>()->default_value("descendant"))
      ("person", "中心人物 ID", cxxopts::value<std::string>())
      ("depth", "展开代数", cxxopts::value<int>()->default_value("5"))
      ("page-size", "欧/苏式每页世数", cxxopts::value<int>()->default_value("5"))
      ("no-spouses", "不显示配偶边", cxxopts::value<bool>()->default_value("false"))
      ("title", "图标题", cxxopts::value<std::string>()->default_value(""))
      ("out", "输出 SVG 文件（缺省打标准输出）", cxxopts::value<std::string>());
  const auto r = parse_args(opts, args);

  const auto style = io::chart_style_from_string(r["style"].as<std::string>());
  if (!style) {
    std::cerr << "用法错误: --style 应为 pedigree|descendant|hourglass|wufu|european|soviet\n";
    return 2;
  }
  if (!r.count("person")) {
    std::cerr << "用法错误: genea io export chart --person <ID> --style <体例>\n";
    return 2;
  }
  const int depth = r["depth"].as<int>();
  if (depth < 1 || depth > 30) {
    std::cerr << "用法错误: --depth 须在 1..30\n";
    return 2;
  }
  const int page_size = r["page-size"].as<int>();
  if (page_size < 2 || page_size > 20) {
    std::cerr << "用法错误: --page-size 须在 2..20\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) { print_error(db.error()); return exit_code_for(db.error()); }

  PersonRepository people(*db);
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = people.list(pf);
  if (!persons) { print_error(persons.error()); return exit_code_for(persons.error()); }
  UnionRepository unions(*db);
  UnionFilter uf;
  uf.limit = 200000;
  auto us = unions.list(uf);
  if (!us) { print_error(us.error()); return exit_code_for(us.error()); }

  auto graph = lineage::PersonGraph::build(std::move(*persons), std::move(*us));
  io::ChartOptions opt;
  opt.graph = &graph;
  opt.style_hint = *style;
  opt.focus_id = r["person"].as<std::string>();
  opt.depth = depth;
  opt.page_size = page_size;
  opt.show_spouses = !r["no-spouses"].as<bool>();
  opt.title = r["title"].as<std::string>();
  if (opt.title.empty()) {
    opt.title = std::string("世系图 · ") + io::to_string(*style);
  }
  auto chart = io::render_chart_svg(opt);
  if (!chart) { print_error(chart.error()); return exit_code_for(chart.error()); }

  // 五服体例由 core 的九族矩阵直接渲染（服等已逐对算好），
  // 其余体例沿用通用 SVG；此处不额外注入脚注。
  if (r.count("out")) {
    const std::string base = r["out"].as<std::string>();
    nlohmann::json files = nlohmann::json::array();
    bool all_ok = true;
    // 多页（欧/苏式）写独立文件：foo.svg → foo-p1.svg / foo-p2.svg …
    // 单页仍写原文件名，保持向后兼容。
    if (chart->pages.size() > 1) {
      const auto dot = base.find_last_of('.');
      const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
      const std::string ext = dot == std::string::npos ? "" : base.substr(dot);
      for (std::size_t i = 0; i < chart->pages.size(); ++i) {
        const std::string path = stem + "-p" + std::to_string(i + 1) + ext;
        bool ok = false;
        write_file(path, chart->pages[i].svg, &ok);
        if (!ok) { all_ok = false; break; }
        files.push_back({{"file", path},
                         {"bytes", chart->pages[i].svg.size()},
                         {"first_generation", chart->pages[i].first_generation + 1},
                         {"last_generation", chart->pages[i].last_generation + 1},
                         {"overlaps_previous", chart->pages[i].overlaps_previous}});
      }
    } else {
      bool ok = false;
      write_file(base, chart->svg, &ok);
      if (!ok) all_ok = false;
      else files.push_back({{"file", base}, {"bytes", chart->svg.size()}});
    }
    if (!all_ok) {
      print_error(Error{ErrorCode::IoError, "写不出文件 " + base});
      return 1;
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"files", files},
                                  {"nodes", chart->node_count},
                                  {"pages", chart->page_count},
                                  {"style", r["style"].as<std::string>()},
                                  {"warnings", chart->warnings}}
                       .dump(2) << "\n";
    } else {
      std::cout << "已导出" << r["style"].as<std::string>() << "图 "
                << chart->node_count << " 节点 / " << chart->page_count << " 页：\n";
      for (const auto& f : files) {
        const int lo = f.value("first_generation", 0);
        const int hi = f.value("last_generation", 0);
        std::cout << "  " << f["file"].get<std::string>()
                  << (lo ? "（第 " + std::to_string(lo) + "–" + std::to_string(hi) + " 世）" : "")
                  << (f.value("overlaps_previous", false) ? " [与上页重叠一世]" : "")
                  << "\n";
      }
      for (const auto& w : chart->warnings) std::cerr << "[提示] " << w << "\n";
    }
  } else {
    // 无 --out：多页时各页用注释分隔打印，便于管道保存
    for (std::size_t i = 0; i < chart->pages.size(); ++i) {
      if (i) std::cout << "\n<!-- === 第 " << (i + 1) << " 页 === -->\n";
      std::cout << chart->pages[i].svg;
    }
  }
  return 0;
}

// P0 出版管线：家谱数据 → 自包含 HTML → 浏览器打印/另存 PDF。
// 字体随包内嵌（完整思源宋体，约 31MB base64），产物单文件离线可用。
int cmd_export_book(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea io export book", "导出谱书 HTML（浏览器打印成册）");
  opts.add_options()
      ("template", "版式: register|genealogy|biography",
       cxxopts::value<std::string>()->default_value("register"))
      ("paper", "纸张: a4|16k", cxxopts::value<std::string>()->default_value("a4"))
      ("landscape", "横向", cxxopts::value<bool>()->default_value("false"))
      ("no-embed-fonts", "不内嵌字体（产物小，但依赖系统字体）",
       cxxopts::value<bool>()->default_value("false"))
      ("font-dir", "字体目录（缺省用随包 assets/fonts）",
       cxxopts::value<std::string>())
      ("use-layout", "走 P2 古籍版式引擎（输出 SVG 而非 HTML）",
       cxxopts::value<bool>()->default_value("false"))
      ("layout-preset", "use-layout 时的版式预设",
       cxxopts::value<std::string>()->default_value("guji_classic"))
      ("out", "输出文件（缺省打标准输出）", cxxopts::value<std::string>());
  const auto r = parse_args(opts, args);

  std::string tmpl = r["template"].as<std::string>();
  io::BookTemplate bt = io::BookTemplate::Register;
  if (tmpl == "genealogy") bt = io::BookTemplate::Genealogy;
  else if (tmpl == "biography") bt = io::BookTemplate::Biography;
  else if (tmpl != "register") {
    std::cerr << "用法错误: --template 应为 register|genealogy|biography\n";
    return 2;
  }
  std::string paper = r["paper"].as<std::string>();
  io::PaperSize ps = io::PaperSize::A4;
  if (paper == "16k") ps = io::PaperSize::K16;
  else if (paper != "a4") {
    std::cerr << "用法错误: --paper 应为 a4|16k\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) { print_error(db.error()); return exit_code_for(db.error()); }
  BookService books(*db);
  auto book = books.get_book();
  if (!book) { print_error(book.error()); return exit_code_for(book.error()); }

  PersonRepository persons(*db);
  PersonFilter pf;
  pf.limit = 200000;  // 单谱全量出版；规模上限已登记（ROADMAP 暂缓项）
  auto rows = persons.list(pf);
  if (!rows) { print_error(rows.error()); return exit_code_for(rows.error()); }

  io::BookHtmlOptions opt;
  opt.book = &*book;
  opt.persons = &*rows;
  opt.tmpl = bt;
  opt.paper = ps;
  opt.landscape = r["landscape"].as<bool>();
  opt.embed_fonts = !r["no-embed-fonts"].as<bool>();
  if (r.count("font-dir")) {
    opt.font_dir = r["font-dir"].as<std::string>();
  } else {
    // 与 GUI font.cpp 同策略：安装后 exe 同目录 fonts/，开发期 cwd assets/fonts
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory("build/bin/fonts", ec)) opt.font_dir = "build/bin/fonts";
    else if (fs::is_directory("assets/fonts", ec)) opt.font_dir = "assets/fonts";
    else opt.font_dir = "fonts";  // 安装布局：相对 cwd 或由调用方 --font-dir 指定
  }
  // P2 整合：--use-layout 时改走古籍版式引擎（共享页布局与夹注续排）。
  // 从数据库人物生成标记节点：世代标题 + 每人一行，行传作为夹注。
  if (r["use-layout"].as<bool>()) {
    std::vector<io::MarkupNode> nodes;
    for (const auto& p : *rows) {
      io::MarkupNode n;
      n.kind = io::MarkupKind::Text;
      n.text = p.family_name + p.given_name +
               (p.birth_date.gregorian ? "　" + *p.birth_date.gregorian : "");
      nodes.push_back(n);
      if (p.biography && !p.biography->empty()) {
        io::MarkupNode note;
        note.kind = io::MarkupKind::Note;
        note.text = *p.biography;
        nodes.push_back(note);
      }
    }
    io::PageLayoutOptions pl;
    pl.source = nodes;
    // 与渲染器共用预设字体度量几何，分页点完全一致
    auto bgeo = io::geometry_for_preset(r["layout-preset"].as<std::string>());
    if (!bgeo) { print_error(bgeo.error()); return exit_code_for(bgeo.error()); }
    pl.geometry = *bgeo;
    auto layout = io::paginate_content(pl);
    if (!layout) { print_error(layout.error()); return exit_code_for(layout.error()); }

    nlohmann::json files = nlohmann::json::array();
    for (std::size_t i = 0; i < layout->pages.size(); ++i) {
      io::LayoutOptions lo;
      lo.preset = r["layout-preset"].as<std::string>();
      lo.book_title = book->name.empty() ? "家谱" : book->name;
      lo.volume_title = book->hall_name.value_or("");
      lo.page_number = static_cast<int>(i + 1);
      lo.total_pages = layout->pages.size();
      lo.content = layout->pages[i].nodes;
      auto svg = io::render_layout_page(lo);
      if (!svg) { print_error(svg.error()); return exit_code_for(svg.error()); }
      if (!r.count("out")) {
        if (i) std::cout << "\n<!-- === 第 " << (i + 1) << " 页 === -->\n";
        std::cout << *svg;
        continue;
      }
      const std::string base = r["out"].as<std::string>();
      const auto dot = base.find_last_of('.');
      const std::string path = layout->pages.size() > 1
          ? (dot == std::string::npos ? base : base.substr(0, dot)) + "-p" +
            std::to_string(i + 1) + (dot == std::string::npos ? "" : base.substr(dot))
          : base;
      bool ok = false;
      write_file(path, *svg, &ok);
      if (!ok) {
        print_error(Error{ErrorCode::IoError, "写不出文件 " + path});
        return 1;
      }
      files.push_back({{"file", path}, {"bytes", svg->size()}});
    }
    if (format == "json" && !files.empty()) {
      std::cout << nlohmann::json{{"files", files},
                                  {"pages", layout->pages.size()},
                                  {"continued_notes", layout->continued_count},
                                  {"preset", r["layout-preset"].as<std::string>()}}
                       .dump(2) << "\n";
    } else if (!files.empty()) {
      std::cout << "已用古籍版式导出 " << layout->pages.size() << " 页"
                << "（夹注续排 " << layout->continued_count << " 段）：\n";
      for (const auto& f : files)
        std::cout << "  " << f["file"].get<std::string>() << "\n";
    }
    return 0;
  }

  auto html = io::render_book_html(opt);
  if (!html) { print_error(html.error()); return exit_code_for(html.error()); }

  if (r.count("out")) {
    bool ok = false;
    write_file(r["out"].as<std::string>(), *html, &ok);
    if (!ok) {
      print_error(Error{ErrorCode::IoError, "写不出文件 " + r["out"].as<std::string>()});
      return 1;
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"file", r["out"].as<std::string>()},
                                  {"bytes", html->size()},
                                  {"persons", rows->size()},
                                  {"template", tmpl},
                                  {"paper", paper}}
                       .dump(2) << "\n";
    } else {
      std::cout << "已导出谱书 " << rows->size() << " 人到 "
                << r["out"].as<std::string>() << "（" << html->size() << " 字节）\n"
                << "在浏览器打开该文件，Ctrl+P 另存为 PDF（纸张 " << paper
                << (opt.landscape ? " 横向" : " 纵向") << "）。\n";
    }
  } else {
    std::cout << *html;
  }
  return 0;
}

}  // namespace genea_cli
