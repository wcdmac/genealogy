// genea：家谱管理命令行（M0）。
// 命令树见设计文档 5.3；全局参数 --db / --format 位于子命令之前。
// 退出码：0 成功 / 1 运行时错误 / 2 用法错误 / 3 不存在 / 4 版本冲突。

#include <cxxopts.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "cli_util.hpp"
#include "cmd_entity.hpp"

#include <genealogy/error.hpp>
#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/generation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/union_service.hpp>

namespace {

using namespace genealogy;

// 退出码映射 / 错误输出 / cxxopts 适配 / 开库 / 日期解析见 cli_util.hpp
// （与 cmd_entity.cpp 共用）。kDefaultLineageId 见 bootstrap.hpp。
using genea_cli::exit_code_for;
using genea_cli::open_db;
using genea_cli::parse_args;
using genea_cli::parse_date_arg;
using genea_cli::print_error;

void print_person_text(const Person& p) {
  std::cout << p.id << "\t" << p.family_name << p.given_name;
  if (p.courtesy_name) std::cout << "（字 " << *p.courtesy_name << "）";
  std::cout << "\t" << to_string(p.gender) << "\t生:" << p.birth_date.display();
  if (p.death_date) std::cout << "\t卒:" << p.death_date->display();
  if (p.generation_no) std::cout << "\t第" << *p.generation_no << "世";
  if (p.is_living) std::cout << "\t[在世]";
  std::cout << "\trev=" << p.rev << "\n";
}

void print_person_json(const Person& p) {
  std::cout << to_json(p).dump(2) << "\n";
}

void print_persons(const std::vector<Person>& persons,
                   const std::string& format) {
  if (format == "json") {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : persons) arr.push_back(to_json(p));
    std::cout << arr.dump(2) << "\n";
    return;
  }
  for (const auto& p : persons) print_person_text(p);
  if (persons.empty()) std::cout << "（无记录）\n";
}

// cxxopts 适配、开库、日期解析见 cli_util.hpp。

int cmd_db_init(const std::string& db_path, const std::string& format) {
  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  if (format == "json") {
    nlohmann::json out{{"database", db_path},
                       {"schema_version", db->schema_version()},
                       {"status", "ok"}};
    std::cout << out.dump(2) << "\n";
  } else {
    std::cout << "数据库就绪: " << db_path
              << "（schema v" << db->schema_version() << "）\n";
  }
  return 0;
}

int cmd_person_add(const std::vector<std::string>& args,
                   const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea person add", "新增人物");
  opts.add_options()
      ("family-name", "姓", cxxopts::value<std::string>())
      ("given-name", "名（谱名）", cxxopts::value<std::string>())
      ("gender", "male|female|unknown",
       cxxopts::value<std::string>()->default_value("unknown"))
      ("birth", "出生日期：ISO（1864-05-09）或老谱原文（同治三年四月初四）",
       cxxopts::value<std::string>())
      ("death", "卒年，格式同 --birth", cxxopts::value<std::string>())
      ("courtesy-name", "字", cxxopts::value<std::string>())
      ("branch", "支派 ID", cxxopts::value<std::string>())
      ("generation", "世代序号", cxxopts::value<int>())
      ("birth-order", "行几", cxxopts::value<int>())
      ("father", "父亲 ID", cxxopts::value<std::string>())
      ("mother", "母亲 ID", cxxopts::value<std::string>())
      ("lineage", "宗族 ID（缺省落入占位宗族）", cxxopts::value<std::string>())
      ("biography", "世系录 / 行传", cxxopts::value<std::string>())
      ("living", "在世标记",
       cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
  const auto r = parse_args(opts, args);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PersonService svc(*db);

  Person p;
  p.lineage_id =
      r.count("lineage") ? r["lineage"].as<std::string>() : kDefaultLineageId;
  if (r.count("family-name")) p.family_name = r["family-name"].as<std::string>();
  if (r.count("given-name")) p.given_name = r["given-name"].as<std::string>();
  if (r.count("courtesy-name")) p.courtesy_name = r["courtesy-name"].as<std::string>();
  if (r.count("branch")) p.branch_id = r["branch"].as<std::string>();
  if (r.count("biography")) p.biography = r["biography"].as<std::string>();
  if (r.count("father")) p.father_id = r["father"].as<std::string>();
  if (r.count("mother")) p.mother_id = r["mother"].as<std::string>();
  if (r.count("generation")) p.generation_no = r["generation"].as<int>();
  if (r.count("birth-order")) p.birth_order = r["birth-order"].as<int>();
  p.is_living = r["living"].as<bool>();

  if (auto g = gender_from_string(r["gender"].as<std::string>()); g) {
    p.gender = *g;
  } else {
    print_error(g.error());
    return exit_code_for(g.error());
  }
  if (r.count("birth")) {
    auto d = parse_date_arg(r["birth"].as<std::string>());
    if (!d) {
      print_error(d.error());
      return exit_code_for(d.error());
    }
    p.birth_date = *d;
  }
  if (r.count("death")) {
    auto d = parse_date_arg(r["death"].as<std::string>());
    if (!d) {
      print_error(d.error());
      return exit_code_for(d.error());
    }
    p.death_date = *d;
  }

  auto created = svc.create_person(p);
  if (!created) {
    print_error(created.error());
    return exit_code_for(created.error());
  }
  if (format == "json") {
    print_person_json(*created);
  } else {
    std::cout << "已建档: ";
    print_person_text(*created);
  }
  return 0;
}

int cmd_person_list(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea person list", "人物列表");
  opts.add_options()
      ("branch", "按支派筛选", cxxopts::value<std::string>())
      ("generation", "按世代筛选", cxxopts::value<int>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("cursor", "游标（上页最后一条 id）", cxxopts::value<std::string>());
  const auto r = parse_args(opts, args);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PersonService svc(*db);
  PersonFilter filter;
  if (r.count("branch")) filter.branch_id = r["branch"].as<std::string>();
  if (r.count("generation")) filter.generation_no = r["generation"].as<int>();
  if (r.count("cursor")) filter.cursor = r["cursor"].as<std::string>();
  filter.limit = r["limit"].as<int>();

  auto list = svc.list_persons(filter);
  if (!list) {
    print_error(list.error());
    return exit_code_for(list.error());
  }
  print_persons(*list, format);
  return 0;
}

int cmd_person_show(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea person show", "人物详情");
  opts.add_options()("id", "人物 ID", cxxopts::value<std::string>());
  opts.parse_positional({"id"});
  const auto r = parse_args(opts, args);
  if (!r.count("id")) {
    std::cerr << "用法错误: genea person show <id>\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PersonService svc(*db);
  auto got = svc.get_person(r["id"].as<std::string>());
  if (!got) {
    print_error(got.error());
    return exit_code_for(got.error());
  }
  if (format == "json") {
    print_person_json(*got);
  } else {
    print_person_text(*got);
  }
  return 0;
}

// person relation <ego> <alter>：世系算法端到端入口（M1.3）。
// 输出世代差、五服（明清定型版）、称谓。图数据全量装载
// （M1 规模足够；万级以上的按需加载留待 M2 仓储层）。
int cmd_person_relation(const std::vector<std::string>& args,
                        const std::string& db_path,
                        const std::string& format) {
  cxxopts::Options opts("genea person relation", "两人关系：世代/五服/称谓");
  opts.add_options()
    ("ego", "关系主体（己方）ID", cxxopts::value<std::string>())
    ("alter", "关系对象 ID", cxxopts::value<std::string>());
  opts.parse_positional({"ego", "alter"});
  const auto r = parse_args(opts, args);
  if (!r.count("ego") || !r.count("alter")) {
    std::cerr << "用法错误: genea person relation <ego_id> <alter_id>\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }

  PersonService persons(*db);
  PersonFilter person_filter;
  person_filter.limit = 100000;  // M1：全量装载
  auto person_list = persons.list_persons(person_filter);
  if (!person_list) {
    print_error(person_list.error());
    return exit_code_for(person_list.error());
  }
  UnionService unions(*db);
  UnionFilter union_filter;
  union_filter.limit = 100000;
  auto union_list = unions.list_unions(union_filter);
  if (!union_list) {
    print_error(union_list.error());
    return exit_code_for(union_list.error());
  }

  // 图持有指向下述 vector 的指针，两者须存活至本函数结束
  auto person_rows = std::move(*person_list);
  auto union_rows = std::move(*union_list);
  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(person_rows, union_rows);

  const std::string ego = r["ego"].as<std::string>();
  const std::string alter = r["alter"].as<std::string>();
  const auto mourning =
      lineage::compute_mourning_degree(graph, lineage::MourningQuery{ego, alter});
  if (!mourning) {
    print_error(mourning.error());
    return exit_code_for(mourning.error());
  }
  const auto appellation =
      lineage::compute_appellation(graph, lineage::AppellationQuery{ego, alter});
  if (!appellation) {
    print_error(appellation.error());
    return exit_code_for(appellation.error());
  }

  const auto rel = lineage::find_relationship(graph, ego, alter);
  const std::string path_key =
      rel.found ? lineage::relationship_key(rel, *graph.find(ego)) : "";

  // 世代差（辈分差）按设计文档 4.4.2：由关系路径的世代升降次数推出——
  // 配偶步不升降，故夫妻同辈、姻亲随配偶计辈（妻之公公 = -1）。
  // 世代序号（自始祖起算）另列：外亲与姻亲常因父母未录而上溯断链，
  // 序号会塌到始祖辈，故不作为辈分差的依据，也允许取不到。
  std::optional<int> gap;
  if (rel.found) gap = rel.generation_gap();
  const auto ego_no = lineage::generation_of(graph, ego);
  const auto alter_no = lineage::generation_of(graph, alter);
  const auto no_gap = lineage::generation_no_gap(graph, ego, alter);

  if (format == "json") {
    nlohmann::json out;
    out["ego"] = ego;
    out["alter"] = alter;
    out["path_key"] = path_key;
    out["generation_gap"] =
        gap ? nlohmann::json(*gap) : nlohmann::json(nullptr);
    nlohmann::json gen_no;
    gen_no["ego"] = ego_no ? nlohmann::json(*ego_no) : nlohmann::json(nullptr);
    gen_no["alter"] =
        alter_no ? nlohmann::json(*alter_no) : nlohmann::json(nullptr);
    gen_no["gap"] = no_gap ? nlohmann::json(*no_gap) : nlohmann::json(nullptr);
    out["generation_no"] = gen_no;
    out["mourning"] = {{"garment", mourning->garment_name},
                       {"period", mourning->period},
                       {"months", mourning->period_months},
                       {"basis", mourning->basis},
                       {"ruleset", mourning->ruleset},
                       {"from_fallback", mourning->from_fallback}};
    out["appellation"] = {{"standard", appellation->standard},
                          {"colloquial", appellation->colloquial},
                          {"formal", appellation->formal},
                          {"basis", appellation->basis},
                          {"from_fallback", appellation->from_fallback}};
    std::cout << out.dump(2) << "\n";
    return 0;
  }

  std::cout << "关系路径: " << (path_key.empty() ? "（无）" : path_key) << "\n";
  if (gap) {
    std::cout << "世代差: " << *gap << "（";
    if (*gap < 0) {
      std::cout << "尊长 " << -*gap << " 辈";
    } else if (*gap > 0) {
      std::cout << "晚幼 " << *gap << " 辈";
    } else {
      std::cout << "同辈";
    }
    std::cout << "）\n";
  } else {
    std::cout << "世代差: （无路径，不可判）\n";
  }
  std::cout << "世代序号: ";
  if (ego_no && alter_no) {
    std::cout << "己 第 " << *ego_no << " 世 · 彼 第 " << *alter_no << " 世";
    if (no_gap) std::cout << "（序号差 " << *no_gap << "）";
  } else {
    std::cout << "（一方可上溯断链，序号不可定）";
  }
  std::cout << "\n";
  std::cout << "五服[" << mourning->ruleset << "]: " << mourning->garment_name
            << " " << mourning->period;
  if (mourning->period_months > 0) {
    std::cout << "（" << mourning->period_months << " 月）";
  }
  std::cout << " — " << mourning->basis;
  if (mourning->from_fallback) std::cout << "（兜底推断）";
  std::cout << "\n";
  std::cout << "称谓: " << appellation->standard << "（口语: "
            << appellation->colloquial << "；书面: " << appellation->formal
            << "）\n";
  return 0;
}

}  // namespace

#ifdef _WIN32
// Windows 下 main 的 argc/argv 由 CRT 按 ANSI 代码页（中文机为 936/GBK）
// 转换，中文参数会变成非法 UTF-8 字节（nlohmann 抛 type_error.316）。
// 因此 Windows 入口用 wmain 拿 UTF-16 原始命令行，手动转 UTF-8。
std::string wide_to_utf8(const wchar_t* w) {
  if (w == nullptr) return {};
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), size, nullptr, nullptr);
  return out;
}
#endif

// args 不含程序名（parse_args 内部会补 argv[0] 占位）。
int run(std::vector<std::string> raw_args) {
  cxxopts::Options global("genea", "家谱管理命令行（M0）");
  global.add_options()
      ("h,help", "显示帮助")
      ("db", "SQLite 数据库路径",
       cxxopts::value<std::string>()->default_value("genealogy.db"))
      ("format", "输出格式: text|json",
       cxxopts::value<std::string>()->default_value("text"))
      ("v,verbose", "详细输出");
  global.allow_unrecognised_options();

  cxxopts::ParseResult g;
  try {
    g = parse_args(global, raw_args);
  } catch (const std::exception& e) {
    std::cerr << "用法错误: " << e.what() << "\n";
    return 2;
  }

  const auto& unmatched = g.unmatched();
  if (g.count("help") > 0) {
    std::cout << global.help() << "\n子命令: db init | person add|list|show|relation |"
              << " lineage add|list|show|remove |"
              << " branch add|list|show|remove |"
              << " place add|list|show|remove |"
              << " union add|list|show|remove |"
              << " event add|list|show|remove\n";
    return 0;
  }
  if (unmatched.empty()) {
    std::cout << global.help() << "\n子命令: db init | person add|list|show|relation |"
              << " lineage add|list|show|remove |"
              << " branch add|list|show|remove |"
              << " place add|list|show|remove |"
              << " union add|list|show|remove |"
              << " event add|list|show|remove\n";
    return 2;
  }

  const std::string db_path = g["db"].as<std::string>();
  const std::string format = g["format"].as<std::string>();
  if (format != "text" && format != "json") {
    std::cerr << "用法错误: --format 仅支持 text|json\n";
    return 2;
  }

  const std::string command = unmatched[0];
  const std::vector<std::string> rest(unmatched.begin() + 1, unmatched.end());
  if (rest.empty()) {
    std::cerr << "用法错误: " << command << " 缺少子命令\n";
    return 2;
  }
  const std::string sub = rest[0];
  const std::vector<std::string> args(rest.begin() + 1, rest.end());

  try {
    if (command == "db" && sub == "init") return cmd_db_init(db_path, format);
    if (command == "person") {
      if (sub == "add") return cmd_person_add(args, db_path, format);
      if (sub == "list") return cmd_person_list(args, db_path, format);
      if (sub == "show") return cmd_person_show(args, db_path, format);
      if (sub == "relation") return cmd_person_relation(args, db_path, format);
    }
    // 实体命令组内部自己拆子命令（add/list/show/remove），故传 rest
    // （含子命令）；person/db 系列直接消费 options，传 args（不含子命令）。
    if (command == "lineage")
      return genea_cli::cmd_lineage(rest, db_path, format);
    if (command == "branch")
      return genea_cli::cmd_branch(rest, db_path, format);
    if (command == "place")
      return genea_cli::cmd_place(rest, db_path, format);
    if (command == "union")
      return genea_cli::cmd_union(rest, db_path, format);
    if (command == "event")
      return genea_cli::cmd_event(rest, db_path, format);
  } catch (const cxxopts::exceptions::exception& e) {
    // 参数/用法问题
    std::cerr << "用法错误: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    // 运行期异常（如 UTF-8 校验失败），不是用户用法问题
    std::cerr << "内部错误: " << e.what() << "\n";
    return 1;
  }

  std::cerr << "未知命令: " << command << " " << sub
            << "（支持: db init | person add|list|show |"
            << " lineage|branch|place|union|event add|list|show|remove）\n";
  return 2;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
  std::vector<std::string> args;
  if (argc > 1) {
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) args.push_back(wide_to_utf8(argv[i]));
  }
  return run(std::move(args));
}
#else
int main(int argc, char* argv[]) {
  std::vector<std::string> args;
  if (argc > 1) {
    args.reserve(static_cast<std::size_t>(argc - 1));
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  }
  return run(std::move(args));
}
#endif
