// genea：家谱管理命令行（M0）。
// 命令树见设计文档 5.3；全局参数 --db / --format 位于子命令之前。
// 退出码：0 成功 / 1 运行时错误 / 2 用法错误 / 3 不存在 / 4 版本冲突。

#include <cxxopts.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "cli_util.hpp"
#include "cmd_backup.hpp"
#include "cmd_entity.hpp"
#include "cmd_io.hpp"
#include "cmd_migration.hpp"
#include "cmd_poem.hpp"
#include "cmd_source.hpp"
#include "cmd_document.hpp"

#include <genealogy/calendar/chinese_calendar.hpp>
#include <genealogy/calendar/era.hpp>
#include <genealogy/error.hpp>
#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/generation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>
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

// 子命令总览。**加新命令组时同步更新这里**——曾经漏更过（backup/
// restore/poem/migration 上线后帮助文本还是旧的一份，新功能对
// 用户不可见，而没有任何测试会因此变红）。
const char* kSubcommandsHelp =
    "子命令:\n"
    "  db       init|seed|backup|restore\n"
    "  person   add|list|show|relation|tree\n"
    "  lineage  add|list|show|remove\n"
    "  branch   add|list|show|remove\n"
    "  place    add|list|show|remove\n"
    "  union    add|list|show|remove\n"
    "  event    add|list|show|remove\n"
    "  migration add|list|show|remove\n"
    "  poem     set|show|check\n"
    "  date     convert|lunar|terms|era|info\n"
    "  io       import|export|validate\n"
    "  bench";

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

// person tree <id>：十代子孙树（M1.6 性能验收的查询入口）。
// 图数据全量装载（M1 规模足够；万级以上的按需加载留待 M2 仓储层）。
int cmd_person_tree(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea person tree", "后代树（子孙树）");
  opts.add_options()
      ("id", "起点人物 ID", cxxopts::value<std::string>())
      ("depth", "展开层数（1 = 只有本人）",
       cxxopts::value<int>()->default_value("10"))
      ("limit", "全量装载上限", cxxopts::value<int>()->default_value("200000"));
  opts.parse_positional({"id"});
  const auto r = parse_args(opts, args);
  if (!r.count("id")) {
    std::cerr << "用法错误: genea person tree <id> [--depth 10]\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PersonService persons(*db);
  PersonFilter person_filter;
  person_filter.limit = r["limit"].as<int>();
  auto person_list = persons.list_persons(person_filter);
  if (!person_list) {
    print_error(person_list.error());
    return exit_code_for(person_list.error());
  }
  UnionService unions(*db);
  UnionFilter union_filter;
  union_filter.limit = 200000;
  auto union_list = unions.list_unions(union_filter);
  if (!union_list) {
    print_error(union_list.error());
    return exit_code_for(union_list.error());
  }
  auto person_rows = std::move(*person_list);
  auto union_rows = std::move(*union_list);
  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(person_rows, union_rows);

  const auto t0 = std::chrono::steady_clock::now();
  const auto tree = lineage::descendants_tree(graph, r["id"].as<std::string>(),
                                              r["depth"].as<int>());
  const auto t1 = std::chrono::steady_clock::now();
  if (!tree) {
    print_error(tree.error());
    return exit_code_for(tree.error());
  }
  const double ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  const std::size_t nodes = lineage::tree_node_count(*tree);
  const int depth_seen = lineage::tree_depth(*tree);

  if (format == "json") {
    nlohmann::json out;
    out["root"] = r["id"].as<std::string>();
    out["nodes"] = nodes;
    out["depth"] = depth_seen;
    out["elapsed_ms"] = ms;
    std::cout << out.dump(2) << "\n";
    return 0;
  }
  // 文本树：缩进两级/代，节点数过多时只打前 200 个
  std::function<void(const lineage::TreeNode&, int, std::size_t*)> print =
      [&](const lineage::TreeNode& node, int level, std::size_t* budget) {
        if (*budget == 0) return;
        --*budget;
        const Person* p = node.person;
        std::cout << std::string(static_cast<std::size_t>(level) * 2, ' ')
                  << (p ? p->family_name + p->given_name : "（佚名）");
        if (p && p->generation_no) std::cout << "［第" << *p->generation_no << "世］";
        if (p && p->birth_date.gregorian) std::cout << " " << *p->birth_date.gregorian;
        std::cout << "\n";
        for (const auto& c : node.children) print(c, level + 1, budget);
      };
  std::size_t budget = 200;
  print(*tree, 0, &budget);
  if (budget == 0) std::cout << "…（只显示前 200 个节点）\n";
  std::cout << "后代树: " << nodes << " 节点 / " << depth_seen << " 层，耗时 "
            << ms << " ms\n";
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

// ---------------------------------------------------------------- date
// 历法引擎入口（M1.4）：公历↔农历、干支、节气。纯计算，不碰数据库。
// 数据覆盖 1583-3000（见 first_supported_year / last_supported_year），
// 1900-2025 已与紫金山天文台官方日历逐日核对。

namespace {

using namespace genealogy;
using genea_cli::exit_code_for;
using genea_cli::parse_args;
using genea_cli::print_error;

// 解析 ISO 日期：yyyy | yyyy-mm | yyyy-mm-dd（缺省月日补 1）。
// 只做字面合法性检查；具体日期是否存在（2 月 30 日之类）交给历法引擎。
bool parse_iso_date(const std::string& s, calendar::GregorianDate* out) {
  std::vector<std::string> parts;
  std::string cur;
  for (const char c : s) {
    if (c == '-') {
      parts.push_back(cur);
      cur.clear();
    } else if (c < '0' || c > '9') {
      return false;
    } else {
      cur += c;
    }
  }
  parts.push_back(cur);
  if (parts.empty() || parts.size() > 3) return false;
  if (parts[0].empty() || parts[0].size() > 4) return false;
  try {
    out->year = std::stoi(parts[0]);
    out->month = parts.size() > 1 ? std::stoi(parts[1]) : 1;
    out->day = parts.size() > 2 ? std::stoi(parts[2]) : 1;
  } catch (const std::exception&) {
    return false;
  }
  return out->month >= 1 && out->month <= 12 && out->day >= 1 && out->day <= 31;
}

int days_in_month(int y, int m) {
  static const int kLen[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return kLen[m];
}

std::string iso_of(const calendar::GregorianDate& d) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
  return buf;
}

// 公历 → 农历 + 干支 + 当日节气
int cmd_date_convert(const std::vector<std::string>& args,
                     const std::string& format) {
  cxxopts::Options opts("genea date convert", "公历转农历（含干支、节气）");
  opts.add_options()("date", "公历日期 ISO：yyyy-mm-dd",
                     cxxopts::value<std::string>());
  opts.parse_positional({"date"});
  const auto r = parse_args(opts, args);
  if (!r.count("date")) {
    std::cerr << "用法错误: genea date convert <yyyy-mm-dd>\n";
    return 2;
  }
  calendar::GregorianDate g;
  if (!parse_iso_date(r["date"].as<std::string>(), &g)) {
    std::cerr << "用法错误: 日期须为 yyyy-mm-dd（可省 mm/dd）\n";
    return 2;
  }
  if (g.day > days_in_month(g.year, g.month)) {
    std::cerr << "用法错误: " << g.year << " 年 " << g.month << " 月没有 "
              << g.day << " 日\n";
    return 2;
  }

  const auto c = calendar::to_chinese(g);
  if (!c) {
    print_error(c.error());
    return exit_code_for(c.error());
  }
  const auto term = calendar::solar_term_on(g);
  std::string hhmm;
  if (term) {
    const auto st = calendar::solar_term(g.year, *term);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", st->hour, st->minute);
    hhmm = buf;
  }
  const auto era = calendar::era_of(c->year);
  std::string era_text;
  if (era) {
    era_text = calendar::era_display(*era, c->year - era->start_year + 1);
  }

  if (format == "json") {
    nlohmann::json out;
    out["gregorian"] = iso_of(g);
    out["lunar"] = {{"year", c->year},
                    {"month", c->month},
                    {"month_name", calendar::month_name(c->month)},
                    {"day", c->day},
                    {"day_name", calendar::day_name(c->day)},
                    {"is_leap_month", c->is_leap_month()},
                    {"year_ganzhi", calendar::year_ganzhi(c->year)},
                    {"month_ganzhi", calendar::month_ganzhi(c->year, c->month)},
                    {"day_ganzhi", calendar::day_ganzhi(g)},
                    {"zodiac", calendar::zodiac(c->year)}};
    if (term) {
      const auto st = calendar::solar_term(g.year, *term);
      out["solar_term"] = {{"index", *term},
                           {"name", st->name},
                           {"at", hhmm}};
    } else {
      out["solar_term"] = nullptr;
    }
    if (era) {
      out["era"] = {{"dynasty", era->dynasty},
                    {"reign", era->reign},
                    {"year", c->year - era->start_year + 1},
                    {"display", era_text}};
    } else {
      out["era"] = nullptr;
    }
    std::cout << out.dump(2) << "\n";
    return 0;
  }

  if (era) std::cout << "年号 " << era->dynasty << "·" << era_text << "\n";
  std::cout << "公历 " << iso_of(g) << "\n";
  std::cout << "农历 " << calendar::year_ganzhi(c->year) << "年（" << c->year
            << "）" << calendar::month_name(c->month)
            << calendar::day_name(c->day) << "\n";
  std::cout << "纪年 " << calendar::year_ganzhi(c->year) << "年 · "
            << calendar::zodiac(c->year) << " · 月 "
            << calendar::month_ganzhi(c->year, c->month) << " · 日 "
            << calendar::day_ganzhi(g) << "\n";
  if (term) {
    const auto st = calendar::solar_term(g.year, *term);
    std::cout << "节气 " << st->name << "（" << hhmm << "）\n";
  } else {
    std::cout << "节气 （当日无节气）\n";
  }
  return 0;
}

// 农历 → 公历
int cmd_date_lunar(const std::vector<std::string>& args,
                   const std::string& format) {
  cxxopts::Options opts("genea date lunar", "农历转公历");
  opts.add_options()
      ("year", "农历年", cxxopts::value<int>())
      ("month", "农历月 1..12", cxxopts::value<int>())
      ("day", "农历日 1..30", cxxopts::value<int>()->default_value("1"))
      ("leap", "该月为闰月",
       cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
  const auto r = parse_args(opts, args);
  if (!r.count("year") || !r.count("month")) {
    std::cerr << "用法错误: genea date lunar --year <年> --month <月> [--day <日>]"
                 " [--leap]\n";
    return 2;
  }
  const int y = r["year"].as<int>();
  const int m = r["month"].as<int>();
  const int d = r["day"].as<int>();
  const bool leap = r["leap"].as<bool>();
  if (m < 1 || m > 12) {
    std::cerr << "用法错误: 农历月须在 1..12（闰月用 --month 6 --leap 表示闰六月）\n";
    return 2;
  }
  calendar::ChineseDate c;
  c.year = y;
  c.month = leap ? -m : m;
  c.day = d;
  const auto g = calendar::to_gregorian(c);
  if (!g) {
    print_error(g.error());
    return exit_code_for(g.error());
  }
  if (format == "json") {
    nlohmann::json out;
    out["lunar"] = {{"year", y},
                    {"month", c.month},
                    {"month_name", calendar::month_name(c.month)},
                    {"day", d},
                    {"day_name", calendar::day_name(d)}};
    out["gregorian"] = iso_of(*g);
    std::cout << out.dump(2) << "\n";
    return 0;
  }
  std::cout << "农历 " << calendar::year_ganzhi(y) << "年"
            << calendar::month_name(c.month) << calendar::day_name(d) << " → 公历 "
            << iso_of(*g) << "\n";
  return 0;
}

// 某年 24 节气（可只查一个）
int cmd_date_terms(const std::vector<std::string>& args,
                   const std::string& format) {
  cxxopts::Options opts("genea date terms", "某年 24 节气（北京时间）");
  opts.add_options()
      ("year", "公历年", cxxopts::value<int>())
      ("index", "只查第几个节气（0=小寒 … 23=冬至）",
       cxxopts::value<int>());
  const auto r = parse_args(opts, args);
  if (!r.count("year")) {
    std::cerr << "用法错误: genea date terms --year <年> [--index <0..23>]\n";
    return 2;
  }
  const int y = r["year"].as<int>();
  const bool only = r.count("index") != 0;
  const int only_index = only ? r["index"].as<int>() : -1;

  nlohmann::json arr = nlohmann::json::array();
  int from = 0, to = 23;
  if (only) {
    if (only_index < 0 || only_index > 23) {
      std::cerr << "用法错误: --index 须在 0..23\n";
      return 2;
    }
    from = to = only_index;
  }
  for (int i = from; i <= to; ++i) {
    const auto st = calendar::solar_term(y, i);
    if (!st) {
      print_error(st.error());
      return exit_code_for(st.error());
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", st->hour, st->minute);
    if (format == "json") {
      arr.push_back({{"index", i},
                     {"name", st->name},
                     {"date", iso_of(st->date)},
                     {"time", buf}});
    } else {
      std::cout << st->name << "  " << iso_of(st->date) << "  " << buf << "\n";
    }
  }
  if (format == "json") {
    nlohmann::json out;
    out["year"] = y;
    out["terms"] = arr;
    std::cout << out.dump(2) << "\n";
  }
  return 0;
}

// 年号纪年 ↔ 年（并可接着解到具体日期）
int cmd_date_era(const std::vector<std::string>& args,
                 const std::string& format) {
  cxxopts::Options opts("genea date era", "年号纪年换算（如「同治三年」）");
  opts.add_options()("text", "年号纪年，如 同治三年", cxxopts::value<std::string>());
  opts.parse_positional({"text"});
  const auto r = parse_args(opts, args);
  if (!r.count("text")) {
    std::cerr << "用法错误: genea date era <年号纪年>（如 同治三年）\n";
    return 2;
  }
  const std::string text = r["text"].as<std::string>();

  // 复用 GenealogyDate 的年号识别，保证与录入口径一致
  const auto parsed = GenealogyDate::from_raw(text);
  if (!parsed || !parsed->era) {
    std::cerr << "用法错误: 未能识别年号纪年（示例: 同治三年 / 万历十五年）\n";
    return 2;
  }
  const Era era = *parsed->era;
  const auto year = calendar::era_to_year(era.name, era.year);
  if (!year) {
    print_error(year.error());
    return exit_code_for(year.error());
  }
  const auto info = calendar::era_of(*year);
  const std::string reign_display =
      info ? calendar::era_display(*info, *year - info->start_year + 1) : text;

  if (format == "json") {
    nlohmann::json out;
    out["input"] = text;
    out["era"] = {{"name", era.name}, {"year", era.year}};
    if (info) out["era"]["dynasty"] = info->dynasty;
    out["year"] = *year;
    out["year_ganzhi"] = calendar::year_ganzhi(*year);
    out["zodiac"] = calendar::zodiac(*year);
    if (auto g = calendar::to_gregorian(
            calendar::ChineseDate{*year, 1, 1})) {
      out["lunar_new_year"] = iso_of(*g);   // 该年正月初一
    }
    std::cout << out.dump(2) << "\n";
    return 0;
  }

  std::cout << reign_display << " → " << *year << " 年（"
            << calendar::year_ganzhi(*year) << "年 · "
            << calendar::zodiac(*year) << "）\n";
  if (const auto g = calendar::to_gregorian(calendar::ChineseDate{*year, 1, 1})) {
    std::cout << "正月初一: " << iso_of(*g) << "\n";
  }
  return 0;
}

// 支持区间
int cmd_date_info(const std::string& format) {
  if (format == "json") {
    nlohmann::json out;
    out["first_year"] = calendar::first_supported_year();
    out["last_year"] = calendar::last_supported_year();
    out["basis"] =
        "月首取定朔（北京时间）；十一月含冬至、无中气置闰（GB/T 33661-2017）";
    out["verified"] = "1900-2025 已与中国科学院紫金山天文台《1900-2025年日历》逐日核对";
    std::cout << out.dump(2) << "\n";
    return 0;
  }
  std::cout << "历法数据覆盖: " << calendar::first_supported_year() << " - "
            << calendar::last_supported_year() << "\n";
  std::cout << "编算口径: 月首取定朔（北京时间）；十一月含冬至、无中气之月置闰"
               "（GB/T 33661-2017）\n";
  std::cout << "核验: 1900-2025 逐日（46021 天）、3024 条节气时刻均与紫金山天文台"
               "《1900-2025年日历》一致\n";
  return 0;
}

int cmd_date(const std::vector<std::string>& rest, const std::string& format) {
  const std::string sub = rest.empty() ? "" : rest[0];
  const std::vector<std::string> args(rest.begin() + 1, rest.end());
  if (sub == "convert") return cmd_date_convert(args, format);
  if (sub == "lunar") return cmd_date_lunar(args, format);
  if (sub == "terms") return cmd_date_terms(args, format);
  if (sub == "era") return cmd_date_era(args, format);
  if (sub == "info") return cmd_date_info(format);
  std::cerr << "用法错误: genea date convert|lunar|terms|era|info\n";
  return 2;
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
    std::cout << global.help() << "\n" << kSubcommandsHelp;
    return 0;
  }
  if (unmatched.empty()) {
    std::cout << global.help() << "\n" << kSubcommandsHelp;
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
    if (command == "db" && sub == "seed") {
      return genea_cli::cmd_db_seed(args, db_path, format);
    }
    if (command == "db" && sub == "backup") {
      return genea_cli::cmd_db_backup(args, db_path, format);
    }
    if (command == "db" && sub == "restore") {
      return genea_cli::cmd_db_restore(args, db_path, format);
    }
    if (command == "person") {
      if (sub == "add") return cmd_person_add(args, db_path, format);
      if (sub == "list") return cmd_person_list(args, db_path, format);
      if (sub == "show") return cmd_person_show(args, db_path, format);
      if (sub == "relation") return cmd_person_relation(args, db_path, format);
      if (sub == "tree") return cmd_person_tree(args, db_path, format);
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
    if (command == "date")
      return cmd_date(rest, format);   // 历法引擎，不需要数据库
    if (command == "io")
      return genea_cli::cmd_io(rest, db_path, format);
    if (command == "bench")
      return genea_cli::cmd_bench(rest, db_path, format);
    if (command == "poem")
      return genea_cli::cmd_poem(rest, db_path, format);
    if (command == "migration")
      return genea_cli::cmd_migration(rest, db_path, format);
    if (command == "source")
      return genea_cli::cmd_source(rest, db_path, format);
    if (command == "citation")
      return genea_cli::cmd_citation(rest, db_path, format);
    if (command == "document")
      return genea_cli::cmd_document(rest, db_path, format);
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
            << " lineage|branch|place|union|event add|list|show|remove |"
            << " poem set|show|check |"
            << " date convert|lunar|terms|era|info |"
            << " io import|export|validate | bench）\n";
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
