// genea 导入导出与校验（M1.5）。
//
//   genea io import gedcom <file> --lineage <id> [--surname 陈] [--no-places]
//   genea io export gedcom [--lineage <id>] [--out file.ged] [--submitter 名字]
//   genea io validate [--rule R1,R7] [--limit n]
//
// 导入走既有用例层（PersonService/UnionService/EventService/PlaceService），
// 因此领域不变量、rev、事务边界全都照旧生效——不是另写一条旁路。

#include "cmd_io.hpp"

#include "cli_util.hpp"

#include <cxxopts.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <genealogy/calendar/era.hpp>
#include <genealogy/error.hpp>
#include <genealogy/io/gedcom.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
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
  *ok = true;
  return ss.str();
}

void write_file(const std::string& path, const std::string& text, bool* ok) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    *ok = false;
    return;
  }
  out << text;
  *ok = static_cast<bool>(out);
}

// 逐条落库，返回失败信息（空 = 全部成功）
std::string persist(Database& db, io::ImportResult& r, int* created) {
  PlaceService places(db);
  PersonService persons(db);
  UnionService unions(db);
  EventService events(db);
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
  return {};
}

int cmd_import_gedcom(const std::vector<std::string>& args,
                      const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea io import gedcom", "导入 GEDCOM 5.5.1 文件");
  opts.add_options()
      ("file", "GEDCOM 文件路径", cxxopts::value<std::string>())
      ("lineage", "落到哪个宗族 ID", cxxopts::value<std::string>())
      ("surname", "NAME 无 /姓/ 结构时的兜底姓", cxxopts::value<std::string>())
      ("no-places", "不自动按名称建地点",
       cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
  opts.parse_positional({"file"});
  const auto r = parse_args(opts, args);
  if (!r.count("file") || !r.count("lineage")) {
    std::cerr << "用法错误: genea io import gedcom <file.ged> --lineage <宗族ID>"
                 " [--surname 陈] [--no-places]\n";
    return 2;
  }

  bool ok = false;
  const std::string bytes = read_file(r["file"].as<std::string>(), &ok);
  if (!ok) {
    std::cerr << "错误[6001]: 读不到文件 " << r["file"].as<std::string>() << "\n";
    return 1;
  }
  auto doc = io::parse_gedcom(bytes);
  if (!doc) {
    print_error(doc.error());
    return exit_code_for(doc.error());
  }

  io::ImportOptions opt;
  opt.lineage_id = r["lineage"].as<std::string>();
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
                       {"places", data.places.size()}};
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
      ("lineage", "只导出这个宗族（缺省导出全部）", cxxopts::value<std::string>())
      ("out", "输出文件路径（缺省打到标准输出）", cxxopts::value<std::string>())
      ("submitter", "提交人姓名", cxxopts::value<std::string>())
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
  if (r.count("lineage")) {
    const std::string lid = r["lineage"].as<std::string>();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&lid](const Person& p) {
                                return p.lineage_id != lid;
                              }),
               rows.end());
  }

  // 结合：按人物逐个取（M1 规模足够），再去重
  UnionService unions(*db);
  std::vector<Union> union_rows;
  {
    std::vector<std::string> seen;
    for (const auto& p : rows) {
      UnionFilter uf;
      uf.person_id = p.id;
      uf.limit = 1000;
      auto got = unions.list_unions(uf);
      if (!got) continue;
      for (auto& u : *got) {
        if (std::find(seen.begin(), seen.end(), u.id) != seen.end()) continue;
        seen.push_back(u.id);
        union_rows.push_back(std::move(u));
      }
    }
  }

  EventService events(*db);
  std::vector<Event> event_rows;
  {
    std::vector<std::string> seen;
    for (const auto& p : rows) {
      EventFilter ef;
      ef.subject_type = EventSubjectType::Person;
      ef.subject_id = p.id;
      ef.limit = 1000;
      auto got = events.list_events(ef);
      if (!got) continue;
      for (auto& e : *got) {
        if (std::find(seen.begin(), seen.end(), e.id) != seen.end()) continue;
        seen.push_back(e.id);
        event_rows.push_back(std::move(e));
      }
    }
  }

  PlaceService places(*db);
  PlaceFilter plf;
  plf.limit = 100000;
  auto place_list = places.list_places(plf);
  std::vector<Place> place_rows =
      place_list ? std::move(*place_list) : std::vector<Place>{};

  io::ExportOptions eo;
  eo.persons = &rows;
  eo.unions = &union_rows;
  eo.events = &event_rows;
  eo.places = &place_rows;
  eo.submitter_name = r.count("submitter") ? r["submitter"].as<std::string>()
                                           : "genealogy";
  eo.source_version = "1.0";
  const std::string text = io::export_gedcom(eo);

  if (r.count("out")) {
    bool ok = false;
    write_file(r["out"].as<std::string>(), text, &ok);
    if (!ok) {
      std::cerr << "错误[6001]: 写不出文件 " << r["out"].as<std::string>() << "\n";
      return 1;
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"file", r["out"].as<std::string>()},
                                  {"bytes", text.size()},
                                  {"persons", rows.size()},
                                  {"unions", union_rows.size()},
                                  {"events", event_rows.size()}}
                       .dump(2)
                << "\n";
    } else {
      std::cout << "已导出 " << text.size() << " 字节到 " << r["out"].as<std::string>()
                << "（人物 " << rows.size() << " · 结合 " << union_rows.size()
                << " · 事件 " << event_rows.size() << "）\n";
    }
    return 0;
  }
  std::cout << text;
  return 0;
}

int cmd_validate(const std::vector<std::string>& args, const std::string& db_path,
                 const std::string& format) {
  cxxopts::Options opts("genea io validate", "运行一致性校验规则");
  opts.add_options()
      ("rule", "只跑这些规则（逗号分隔）", cxxopts::value<std::string>())
      ("lineage", "只校验这个宗族", cxxopts::value<std::string>())
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
  if (r.count("lineage")) {
    const std::string lid = r["lineage"].as<std::string>();
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&lid](const Person& p) { return p.lineage_id != lid; }),
               rows.end());
  }

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

  auto report = validate(graph, &event_rows, vo);
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
    if (sub == "validate") return cmd_validate(rest, db_path, format);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "用法错误: " << e.what() << "\n";
    return 2;
  }
  std::cerr << "用法错误: genea io import gedcom|export gedcom|validate\n";
  return 2;
}

}  // namespace genea_cli
