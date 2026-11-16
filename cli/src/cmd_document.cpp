// 文档命令组（M7.4 谱序/凡例/族规家训/艺文/契约，三端共用 core 用例层）。
//
// CLI 的价值：谱师把老谱的谱序/凡例/艺文整篇录入（脚本化批量），
// 以及 CI/运维的无交互入口。search 子命令是 ROADMAP 7.4 验收门的
// CLI 口径——「文档可按正文检索命中」。

#include "cmd_document.hpp"

#include <cxxopts.hpp>

#include <nlohmann/json.hpp>

#include <genealogy/model/document.hpp>
#include <genealogy/service/document_service.hpp>

#include "cli_util.hpp"

using namespace genealogy;

namespace genea_cli {
namespace {

nlohmann::json document_json(const Document& d) {
  return to_json(d);
}

}  // namespace

int cmd_document(const std::vector<std::string>& args,
                 const std::string& db_path, const std::string& format) {
  if (args.empty()) {
    std::cerr << "用法错误: genea document add|list|show|search|remove\n";
    return 2;
  }
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  cxxopts::Options opts("genea document " + sub, "文档" + sub);
  opts.add_options()
      ("type", "文档类型: preface|rule|regulation|literature|contract|"
               "edition_note", cxxopts::value<std::string>())
      ("lineage", "宗族 ID（list 过滤用）", cxxopts::value<std::string>())
      ("title", "标题（add 必填）", cxxopts::value<std::string>())
      ("author", "作者/撰人", cxxopts::value<std::string>())
      ("date", "年份照录原文（嘉庆四年 等）", cxxopts::value<std::string>())
      ("edition", "所属修次（嘉庆四年修 等）", cxxopts::value<std::string>())
      ("content", "正文全文（FTS 索引列，可多行）", cxxopts::value<std::string>())
      ("seq", "篇序", cxxopts::value<long long>())
      ("query", "检索关键词（search/list 标题过滤用）", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("help", "显示用法");
  const auto r = parse_args(opts, rest);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  DocumentService svc(*db);

  if (sub == "add") {
    Document d;
    if (r.count("lineage")) d.lineage_id = r["lineage"].as<std::string>();
    if (r.count("type")) {
      auto t = document_type_from_string(r["type"].as<std::string>());
      if (!t) {
        std::cerr << "用法错误: --type 应为 preface/rule/regulation/"
                     "literature/contract/edition_note\n";
        return 2;
      }
      d.type = *t;
    }
    if (r.count("title")) d.title = r["title"].as<std::string>();
    if (r.count("author")) d.author = r["author"].as<std::string>();
    if (r.count("date")) d.date_text = r["date"].as<std::string>();
    if (r.count("edition")) d.edition = r["edition"].as<std::string>();
    if (r.count("content")) d.content = r["content"].as<std::string>();
    if (r.count("seq")) d.seq = r["seq"].as<long long>();
    auto made = svc.create_document(d);
    if (!made) {
      print_error(made.error());
      return exit_code_for(made.error());
    }
    if (format == "json") {
      std::cout << document_json(*made).dump(2) << "\n";
    } else {
      std::cout << "已登记文档: " << made->id << "\t" << to_string(made->type)
                << "\t" << made->title << "\n";
    }
    return 0;
  }

  if (sub == "list") {
    DocumentFilter f;
    if (r.count("type")) {
      auto t = document_type_from_string(r["type"].as<std::string>());
      if (t) f.type = *t;
    }
    if (r.count("lineage")) f.lineage_id = r["lineage"].as<std::string>();
    f.limit = r["limit"].as<int>();
    auto rows = svc.list_documents(f);
    if (!rows) {
      print_error(rows.error());
      return exit_code_for(rows.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& d : *rows) arr.push_back(document_json(d));
      std::cout << arr.dump(2) << "\n";
      return 0;
    }
    for (const auto& d : *rows) {
      std::cout << d.id << "\t" << to_string(d.type) << "\t" << d.title;
      if (d.date_text) std::cout << "\t" << *d.date_text;
      std::cout << "\n";
    }
    return 0;
  }

  if (sub == "show") {
    if (rest.empty()) {
      std::cerr << "用法错误: show <id>\n";
      return 2;
    }
    auto got = svc.get_document(rest[0]);
    if (!got) {
      print_error(got.error());
      return exit_code_for(got.error());
    }
    if (format == "json") {
      std::cout << document_json(*got).dump(2) << "\n";
      return 0;
    }
    std::cout << "文档: " << got->id << "\n类型: " << to_string(got->type)
              << "\n标题: " << got->title;
    if (got->author) std::cout << "\n作者: " << *got->author;
    if (got->date_text) std::cout << "\n年份: " << *got->date_text;
    if (got->edition) std::cout << "\n修次: " << *got->edition;
    if (got->seq) std::cout << "\n篇序: " << *got->seq;
    if (got->content) std::cout << "\n正文:\n" << *got->content << "\n";
    return 0;
  }

  if (sub == "search") {
    if (r.count("query") == 0) {
      std::cerr << "用法错误: search --query 关键词（正文/标题/作者全文检索）\n";
      return 2;
    }
    auto rows = svc.search_documents(r["query"].as<std::string>(),
                                     r["limit"].as<int>());
    if (!rows) {
      print_error(rows.error());
      return exit_code_for(rows.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& d : *rows) arr.push_back(document_json(d));
      std::cout << arr.dump(2) << "\n";
      return 0;
    }
    for (const auto& d : *rows) {
      std::cout << d.id << "\t" << to_string(d.type) << "\t" << d.title << "\n";
    }
    return 0;
  }

  if (sub == "remove") {
    if (rest.empty()) {
      std::cerr << "用法错误: remove <id>\n";
      return 2;
    }
    auto removed = svc.remove_document(rest[0]);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    std::cout << "已删除文档 " << rest[0] << "\n";
    return 0;
  }

  std::cerr << "用法错误: genea document add|list|show|search|remove\n";
  return 2;
}

}  // namespace genea_cli
