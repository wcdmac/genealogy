// genea：家谱管理命令行（M0）。
// 命令树见设计文档 5.3；全局参数 --db / --format 位于子命令之前。
// 退出码：0 成功 / 1 运行时错误 / 2 用法错误 / 3 不存在 / 4 版本冲突。

#include <cxxopts.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/person_service.hpp>

namespace {

using namespace genealogy;

int exit_code_for(const Error& err) {
  switch (err.code()) {
    case ErrorCode::InvalidArgument: return 2;
    case ErrorCode::NotFound: return 3;
    case ErrorCode::Conflict: return 4;
    default: return 1;
  }
}

void print_error(const Error& err) {
  std::cerr << "错误[" << static_cast<int>(err.code()) << "]: " << err.message()
            << "\n";
}

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

// cxxopts 3.x 仅提供 parse(argc, argv) 重载，且解析从下标 1 开始
// （argv[0] 被当作程序名跳过）。此处把参数向量转过去，并补一个
// 程序名占位，否则第一个参数会被静默吞掉。
// 指针指向 args 中的字符串与静态字面量，生命周期覆盖本次 parse 调用。
cxxopts::ParseResult parse_args(cxxopts::Options& opts,
                                const std::vector<std::string>& args) {
  std::vector<const char*> argv;
  argv.push_back("genea");  // argv[0]：程序名占位
  argv.reserve(args.size() + 1);
  for (const auto& a : args) argv.push_back(a.c_str());
  int argc = static_cast<int>(argv.size());
  return opts.parse(argc, argv.data());
}

Result<Database> open_db(const std::string& path) {
  auto db = Database::open(path);
  if (db) ensure_default_lineage(*db);
  return db;
}

// 出生/卒年解析：先试 ISO，失败则按老谱原文照录。
Result<GenealogyDate> parse_date_arg(const std::string& text) {
  if (auto iso = GenealogyDate::from_iso(text); iso) return iso;
  return GenealogyDate::from_raw(text);
}

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
  p.lineage_id = kDefaultLineageId;
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
    std::cout << global.help() << "\n子命令: db init | person add|list|show\n";
    return 0;
  }
  if (unmatched.empty()) {
    std::cout << global.help() << "\n子命令: db init | person add|list|show\n";
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
    }
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
            << "（支持: db init | person add|list|show）\n";
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
