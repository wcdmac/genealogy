// 来源与证据引用命令组（M7.2 证据链：CLI/REST/Web 三端共用用例层）。
//
// CLI 的价值：谱师核对纸面文献时批量登记来源与出处（脚本化），
// 以及 CI/运维的无交互入口。

#include "cmd_source.hpp"

#include <cxxopts.hpp>

#include <nlohmann/json.hpp>

#include <genealogy/model/citation.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/service/citation_service.hpp>
#include <genealogy/service/source_service.hpp>

#include "cli_util.hpp"

using namespace genealogy;

namespace genea_cli {
namespace {

nlohmann::json source_json(const Source& s) {
  nlohmann::json j = to_json(s);
  return j;
}

nlohmann::json citation_json(const Citation& c) {
  return to_json(c);
}

}  // namespace

int cmd_source(const std::vector<std::string>& args,
               const std::string& db_path, const std::string& format) {
  if (args.empty()) {
    std::cerr << "用法错误: genea source add|list|show|remove\n";
    return 2;
  }
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  cxxopts::Options opts("genea source " + sub, "来源" + sub);
  opts.add_options()
      ("type", "来源类型: genealogy_book|local_gazetteer|stele|oral|archive|"
               "photo|dna|document|other", cxxopts::value<std::string>())
      ("title", "标题（必填）", cxxopts::value<std::string>())
      ("author", "作者/编纂者", cxxopts::value<std::string>())
      ("year", "年份照录原文（嘉庆四年 等）", cxxopts::value<std::string>())
      ("repository", "收藏机构（上海图书馆 等）", cxxopts::value<std::string>())
      ("uri", "数字化件 URI", cxxopts::value<std::string>())
      ("citation", "规范引文原文", cxxopts::value<std::string>())
      ("note", "备注", cxxopts::value<std::string>())
      ("q", "标题模糊匹配", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("help", "显示用法");
  const auto r = parse_args(opts, rest);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  SourceService svc(*db);

  if (sub == "add") {
    Source src;
    auto t = r.count("type")
                 ? source_type_from_string(r["type"].as<std::string>())
                 : std::nullopt;
    if (!t) {
      std::cerr << "用法错误: --type 必填（genealogy_book/…/other）\n";
      return 2;
    }
    src.type = *t;
    if (r.count("title") == 0) {
      std::cerr << "用法错误: --title 必填\n";
      return 2;
    }
    src.title = r["title"].as<std::string>();
    if (r.count("author")) src.author = r["author"].as<std::string>();
    if (r.count("year")) src.year_text = r["year"].as<std::string>();
    if (r.count("repository")) src.repository = r["repository"].as<std::string>();
    if (r.count("uri")) src.uri = r["uri"].as<std::string>();
    if (r.count("citation")) src.citation_text = r["citation"].as<std::string>();
    if (r.count("note")) src.note = r["note"].as<std::string>();
    auto made = svc.create_source(src);
    if (!made) {
      print_error(made.error());
      return exit_code_for(made.error());
    }
    if (format == "json") {
      std::cout << source_json(*made).dump(2) << "\n";
    } else {
      std::cout << "已登记来源: " << made->id << "\t"
                << to_string(made->type) << "\t" << made->title << "\n";
    }
    return 0;
  }

  if (sub == "list") {
    SourceFilter f;
    if (r.count("type")) {
      auto t = source_type_from_string(r["type"].as<std::string>());
      if (t) f.type = *t;
    }
    if (r.count("q")) f.title_contains = r["q"].as<std::string>();
    f.limit = r["limit"].as<int>();
    auto rows = svc.list_sources(f);
    if (!rows) {
      print_error(rows.error());
      return exit_code_for(rows.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& s : *rows) arr.push_back(source_json(s));
      std::cout << arr.dump(2) << "\n";
      return 0;
    }
    for (const auto& s : *rows) {
      std::cout << s.id << "\t" << to_string(s.type) << "\t" << s.title;
      if (s.year_text) std::cout << "\t" << *s.year_text;
      std::cout << "\n";
    }
    return 0;
  }

  if (sub == "show") {
    if (rest.empty()) {
      std::cerr << "用法错误: show <id>\n";
      return 2;
    }
    auto got = svc.get_source(rest[0]);
    if (!got) {
      print_error(got.error());
      return exit_code_for(got.error());
    }
    if (format == "json") {
      std::cout << source_json(*got).dump(2) << "\n";
      return 0;
    }
    std::cout << "来源: " << got->id << "\n类型: " << to_string(got->type)
              << "\n标题: " << got->title;
    if (got->author) std::cout << "\n作者: " << *got->author;
    if (got->year_text) std::cout << "\n年份: " << *got->year_text;
    if (got->repository) std::cout << "\n收藏: " << *got->repository;
    if (got->uri) std::cout << "\nURI: " << *got->uri;
    if (got->citation_text) std::cout << "\n引文: " << *got->citation_text;
    if (got->note) std::cout << "\n备注: " << *got->note;
    std::cout << "\n";
    return 0;
  }

  if (sub == "remove") {
    if (rest.empty()) {
      std::cerr << "用法错误: remove <id>\n";
      return 2;
    }
    auto removed = svc.remove_source(rest[0]);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    std::cout << "已删除来源 " << rest[0] << "\n";
    return 0;
  }

  std::cerr << "用法错误: 未知子命令\n";
  return 2;
}

int cmd_citation(const std::vector<std::string>& args,
                 const std::string& db_path, const std::string& format) {
  if (args.empty()) {
    std::cerr << "用法错误: genea citation add|list|show|remove\n";
    return 2;
  }
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  cxxopts::Options opts("genea citation " + sub, "证据引用" + sub);
  opts.add_options()
      ("subject-type", "主体类型: person|event|union|migration|branch|lineage",
       cxxopts::value<std::string>())
      ("subject", "主体 ID", cxxopts::value<std::string>())
      ("source", "来源 ID", cxxopts::value<std::string>())
      ("locator", "定位串（卷/页/字段路径）", cxxopts::value<std::string>())
      ("confidence", "置信度: high|medium|low|disputed",
       cxxopts::value<std::string>()->default_value("medium"))
      ("note", "备注", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("help", "显示用法");
  const auto r = parse_args(opts, rest);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  CitationService svc(*db);

  if (sub == "add") {
    Citation c;
    auto st = r.count("subject-type")
                  ? citation_subject_from_string(r["subject-type"].as<std::string>())
                  : std::nullopt;
    if (!st) {
      std::cerr << "用法错误: --subject-type 必填（person/…/lineage）\n";
      return 2;
    }
    c.subject_type = *st;
    if (r.count("subject") == 0 || r.count("source") == 0) {
      std::cerr << "用法错误: --subject 与 --source 必填\n";
      return 2;
    }
    c.subject_id = r["subject"].as<std::string>();
    c.source_id = r["source"].as<std::string>();
    if (r.count("locator")) c.locator = r["locator"].as<std::string>();
    auto conf = confidence_from_string(r["confidence"].as<std::string>());
    if (!conf) {
      std::cerr << "用法错误: --confidence 应为 high/medium/low/disputed\n";
      return 2;
    }
    c.confidence = *conf;
    if (r.count("note")) c.note = r["note"].as<std::string>();
    auto made = svc.create_citation(c);
    if (!made) {
      print_error(made.error());
      return exit_code_for(made.error());
    }
    if (format == "json") {
      std::cout << citation_json(*made).dump(2) << "\n";
    } else {
      std::cout << "已登记证据引用: " << made->id << "\t"
                << to_string(made->subject_type) << "/" << made->subject_id
                << "\t" << made->source_id;
      if (made->locator) std::cout << "\t" << *made->locator;
      std::cout << "\t" << to_string(made->confidence) << "\n";
    }
    return 0;
  }

  if (sub == "list") {
    CitationFilter f;
    if (r.count("subject-type")) {
      auto t = citation_subject_from_string(r["subject-type"].as<std::string>());
      if (t) f.subject_type = *t;
    }
    if (r.count("subject")) f.subject_id = r["subject"].as<std::string>();
    if (r.count("source")) f.source_id = r["source"].as<std::string>();
    f.limit = r["limit"].as<int>();
    auto rows = svc.list_citations(f);
    if (!rows) {
      print_error(rows.error());
      return exit_code_for(rows.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& c : *rows) arr.push_back(citation_json(c));
      std::cout << arr.dump(2) << "\n";
      return 0;
    }
    for (const auto& c : *rows) {
      std::cout << c.id << "\t" << to_string(c.subject_type) << "/"
                << c.subject_id << "\t" << c.source_id;
      if (c.locator) std::cout << "\t" << *c.locator;
      std::cout << "\t" << to_string(c.confidence) << "\n";
    }
    return 0;
  }

  if (sub == "show") {
    if (rest.empty()) {
      std::cerr << "用法错误: show <id>\n";
      return 2;
    }
    auto got = svc.get_citation(rest[0]);
    if (!got) {
      print_error(got.error());
      return exit_code_for(got.error());
    }
    if (format == "json") {
      std::cout << citation_json(*got).dump(2) << "\n";
      return 0;
    }
    std::cout << "引用: " << got->id << "\n主体: " << to_string(got->subject_type)
              << "/" << got->subject_id << "\n来源: " << got->source_id;
    if (got->locator) std::cout << "\n定位: " << *got->locator;
    std::cout << "\n置信度: " << to_string(got->confidence);
    if (got->note) std::cout << "\n备注: " << *got->note;
    std::cout << "\n";
    return 0;
  }

  if (sub == "remove") {
    if (rest.empty()) {
      std::cerr << "用法错误: remove <id>\n";
      return 2;
    }
    auto removed = svc.remove_citation(rest[0]);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    std::cout << "已解除证据引用 " << rest[0] << "\n";
    return 0;
  }

  std::cerr << "用法错误: genea source add|list|show|remove / citation add|list|show|remove\n";
  return 2;
}

}  // namespace genea_cli
