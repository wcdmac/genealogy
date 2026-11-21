// genea 实体命令：lineage / branch / place / union / event。
// 每个命令组四个子命令：add / list / show / remove。
// 退出码约定见 cli_util.hpp（0 成功 / 1 运行时 / 2 用法 / 3 不存在 / 4 冲突）。

#include "cmd_entity.hpp"

#include "cli_util.hpp"

#include <cxxopts.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/branch_service.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/model/book.hpp>
#include <genealogy/service/book_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/union_service.hpp>

namespace genea_cli {
namespace {

using namespace genealogy;

// ---------- 通用输出 ----------

// 列表输出：json 走 to_json 数组；text 调各行打印器。
template <typename T, typename TextFn>
int print_list(const Result<std::vector<T>>& list, const std::string& format,
               TextFn text_fn) {
  if (!list) {
    print_error(list.error());
    return exit_code_for(list.error());
  }
  if (format == "json") {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& v : *list) arr.push_back(to_json(v));
    std::cout << arr.dump(2) << "\n";
    return 0;
  }
  for (const auto& v : *list) text_fn(v);
  if (list->empty()) std::cout << "（无记录）\n";
  return 0;
}

// 单条输出（show / add 共用）。
template <typename T, typename TextFn>
int print_one(const Result<T>& got, const std::string& format,
              TextFn text_fn) {
  if (!got) {
    print_error(got.error());
    return exit_code_for(got.error());
  }
  if (format == "json") {
    std::cout << to_json(*got).dump(2) << "\n";
  } else {
    text_fn(*got);
  }
  return 0;
}

// 用法错误快捷方式。
int usage(const std::string& msg) {
  std::cerr << "用法错误: " << msg << "\n";
  return 2;
}

// ---------- 文本行打印器 ----------

void print_book_text(const Book& v) {
  std::cout << v.surname << "氏·" << v.name;
  if (v.commandery) std::cout << "\t[" << *v.commandery << "]";
  if (v.hall_name) std::cout << "\t" << *v.hall_name;
  std::cout << "\trev=" << v.rev << "\n";
}

void print_branch_text(const Branch& v) {
  std::cout << v.id << "\t" << v.name;
  if (v.generation_char) std::cout << "（辈字:" << *v.generation_char << "）";
  if (v.founder_person_id) std::cout << "\t始迁祖=" << *v.founder_person_id;
  if (v.parent_branch_id) std::cout << "\t父房=" << *v.parent_branch_id;
  std::cout << "\trev=" << v.rev << "\n";
}

void print_place_text(const Place& v) {
  std::cout << v.id << "\t" << v.name;
  if (v.type) std::cout << "\t" << to_string(*v.type);
  if (v.modern_admin) std::cout << "\t今属:" << *v.modern_admin;
  if (v.historical_name) std::cout << "\t沿革:" << *v.historical_name;
  std::cout << "\n";
}

void print_union_text(const Union& v) {
  std::cout << v.id << "\t" << to_string(v.type) << "/" << to_string(v.status);
  if (v.sequence) std::cout << "\t第" << *v.sequence << "任";
  std::cout << "\t成员:";
  for (const auto& m : v.members) {
    std::cout << " " << m.person_id << "(" << to_string(m.role) << ")";
  }
  if (v.start_date) std::cout << "\t自:" << v.start_date->display();
  if (v.end_date) std::cout << "\t至:" << v.end_date->display();
  std::cout << "\trev=" << v.rev << "\n";
}

void print_event_text(const Event& v) {
  std::cout << v.id << "\t" << to_string(v.subject_type) << ":" << v.subject_id
            << "\t" << to_string(v.type);
  if (v.date) std::cout << "\t" << v.date->display();
  if (v.place_id) std::cout << "\t地点=" << *v.place_id;
  if (v.description) std::cout << "\t" << *v.description;
  std::cout << "\n";
}

}  // namespace

// ---------- book（单谱单库：谱元数据，恒单行） ----------

int cmd_book(const std::vector<std::string>& args,
             const std::string& db_path, const std::string& format) {
  if (args.empty()) return usage("genea book <show|update> ...");
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  BookService svc(*db);

  if (sub == "show") {
    return print_one(svc.get_book(), format, print_book_text);
  }

  if (sub == "update") {
    cxxopts::Options opts("genea book update", "更新谱元数据");
    opts.add_options()
      ("name", "谱名", cxxopts::value<std::string>())
      ("surname", "主姓", cxxopts::value<std::string>())
      ("surname-origin", "姓氏源流", cxxopts::value<std::string>())
      ("commandery", "郡望", cxxopts::value<std::string>())
      ("hall", "堂号", cxxopts::value<std::string>())
      ("note", "备注", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    auto got_b = svc.get_book();
    if (!got_b) { print_error(got_b.error()); return exit_code_for(got_b.error()); }
    Book b = *got_b;
    if (r.count("name")) b.name = r["name"].as<std::string>();
    if (r.count("surname")) b.surname = r["surname"].as<std::string>();
    if (r.count("surname-origin"))
      b.surname_origin = r["surname-origin"].as<std::string>();
    if (r.count("commandery"))
      b.commandery = r["commandery"].as<std::string>();
    if (r.count("hall")) b.hall_name = r["hall"].as<std::string>();
    if (r.count("note")) b.note = r["note"].as<std::string>();
    return print_one(svc.update_book(b), format, print_book_text);
  }

  return usage("genea book <show|update> ...");
}

// ---------- branch ----------

int cmd_branch(const std::vector<std::string>& args,
               const std::string& db_path, const std::string& format) {
  if (args.empty()) return usage("genea branch <add|list|show|remove> ...");
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  BranchService svc(*db);

  if (sub == "add") {
    cxxopts::Options opts("genea branch add", "新增支派");
    opts.add_options()
      ("lineage", "所属宗族 ID", cxxopts::value<std::string>())
      ("name", "支派名称", cxxopts::value<std::string>())
      ("founder", "始迁祖（人物 ID）", cxxopts::value<std::string>())
      ("parent", "父房（支派 ID）", cxxopts::value<std::string>())
      ("gen-char", "辈字（派语定位字）", cxxopts::value<std::string>())
      ("note", "备注", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    Branch b;
    if (r.count("name")) b.name = r["name"].as<std::string>();
    if (r.count("founder"))
      b.founder_person_id = r["founder"].as<std::string>();
    if (r.count("parent"))
      b.parent_branch_id = r["parent"].as<std::string>();
    if (r.count("gen-char"))
      b.generation_char = r["gen-char"].as<std::string>();
    if (r.count("note")) b.note = r["note"].as<std::string>();
    return print_one(svc.create_branch(b), format, print_branch_text);
  }

  if (sub == "list") {
    cxxopts::Options opts("genea branch list", "支派列表");
    opts.add_options()
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("cursor", "游标", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    BranchFilter f;
    if (r.count("cursor")) f.cursor = r["cursor"].as<std::string>();
    f.limit = r["limit"].as<int>();
    return print_list(svc.list_branches(f), format, print_branch_text);
  }

  if (sub == "show" || sub == "remove") {
    cxxopts::Options opts("genea branch " + sub, "支派详情/删除");
    opts.add_options()("id", "支派 ID", cxxopts::value<std::string>());
    opts.parse_positional({"id"});
    const auto r = parse_args(opts, rest);
    if (!r.count("id")) return usage("genea branch " + sub + " <id>");
    const std::string id = r["id"].as<std::string>();
    if (sub == "show") {
      return print_one(svc.get_branch(id), format, print_branch_text);
    }
    auto removed = svc.remove_branch(id);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"removed", id}}.dump(2) << "\n";
    } else {
      std::cout << "已删除: " << id << "\n";
    }
    return 0;
  }

  return usage("未知子命令: branch " + sub +
               "（支持: add|list|show|remove）");
}

// ---------- place ----------

int cmd_place(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format) {
  if (args.empty()) return usage("genea place <add|list|show|remove> ...");
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  PlaceService svc(*db);

  if (sub == "add") {
    cxxopts::Options opts("genea place add", "新增地点");
    opts.add_options()
      ("name", "地名（历史名为主名）", cxxopts::value<std::string>())
      ("type", "village|town|county|prefecture|province|grave|"
               "ancestral_hall|other",
       cxxopts::value<std::string>())
      ("modern", "现代政区对照", cxxopts::value<std::string>())
      ("historical", "历代沿革别名", cxxopts::value<std::string>())
      ("lat", "纬度", cxxopts::value<double>())
      ("lng", "经度", cxxopts::value<double>())
      ("note", "备注", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    Place p;
    if (r.count("name")) p.name = r["name"].as<std::string>();
    if (r.count("modern")) p.modern_admin = r["modern"].as<std::string>();
    if (r.count("historical"))
      p.historical_name = r["historical"].as<std::string>();
    if (r.count("lat")) p.lat = r["lat"].as<double>();
    if (r.count("lng")) p.lng = r["lng"].as<double>();
    if (r.count("note")) p.note = r["note"].as<std::string>();
    if (r.count("type")) {
      auto t = place_type_from_string(r["type"].as<std::string>());
      if (!t) return usage("未知地点类型: " + r["type"].as<std::string>());
      p.type = *t;
    }
    return print_one(svc.create_place(p), format, print_place_text);
  }

  if (sub == "list") {
    cxxopts::Options opts("genea place list", "地点列表");
    opts.add_options()
      ("type", "按类型筛选", cxxopts::value<std::string>())
      ("name", "名称模糊匹配", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("cursor", "游标", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    PlaceFilter f;
    if (r.count("type")) {
      auto t = place_type_from_string(r["type"].as<std::string>());
      if (!t) return usage("未知地点类型: " + r["type"].as<std::string>());
      f.type = *t;
    }
    if (r.count("name")) f.name_contains = r["name"].as<std::string>();
    if (r.count("cursor")) f.cursor = r["cursor"].as<std::string>();
    f.limit = r["limit"].as<int>();
    return print_list(svc.list_places(f), format, print_place_text);
  }

  if (sub == "show" || sub == "remove") {
    cxxopts::Options opts("genea place " + sub, "地点详情/删除");
    opts.add_options()("id", "地点 ID", cxxopts::value<std::string>());
    opts.parse_positional({"id"});
    const auto r = parse_args(opts, rest);
    if (!r.count("id")) return usage("genea place " + sub + " <id>");
    const std::string id = r["id"].as<std::string>();
    if (sub == "show") {
      return print_one(svc.get_place(id), format, print_place_text);
    }
    auto removed = svc.remove_place(id);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"removed", id}}.dump(2) << "\n";
    } else {
      std::cout << "已删除: " << id << "\n";
    }
    return 0;
  }

  return usage("未知子命令: place " + sub +
               "（支持: add|list|show|remove）");
}

// ---------- union ----------

// "--member pid:role,pid2:role2"（cxxopts 不支持同名选项累积，用逗号列表）。
Result<std::vector<UnionMember>> parse_members(const std::string& text) {
  std::vector<UnionMember> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t comma = text.find(',', start);
    const std::string item = text.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!item.empty()) {
      const std::size_t colon = item.find(':');
      if (colon == std::string::npos) {
        return invalid_argument("成员格式应为 人物ID:角色（role）: " + item);
      }
      UnionMember m;
      m.person_id = item.substr(0, colon);
      const std::string role_text = item.substr(colon + 1);
      auto role = member_role_from_string(role_text);
      if (!role) return invalid_argument("未知成员角色: " + role_text);
      m.role = *role;
      out.push_back(m);
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (out.empty()) return invalid_argument("--member 不能为空");
  return out;
}

int cmd_union(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format) {
  if (args.empty()) return usage("genea union <add|list|show|remove> ...");
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  UnionService svc(*db);

  if (sub == "add") {
    cxxopts::Options opts("genea union add", "新增婚姻结合");
    opts.add_options()
      ("type", "marriage|concubinage|remarriage",
       cxxopts::value<std::string>()->default_value("marriage"))
      ("status", "active|dissolved",
       cxxopts::value<std::string>()->default_value("active"))
      ("member", "成员：人物ID:角色（husband|wife|concubine），"
                 "多人用逗号分隔",
       cxxopts::value<std::string>())
      ("sequence", "第几任", cxxopts::value<int>())
      ("start", "结合起始日期", cxxopts::value<std::string>())
      ("end", "结合终止日期", cxxopts::value<std::string>())
      ("place", "结合地点 ID", cxxopts::value<std::string>())
      ("note", "备注", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);

    auto type = union_type_from_string(r["type"].as<std::string>());
    if (!type) return usage("未知结合类型: " + r["type"].as<std::string>());
    auto status = union_status_from_string(r["status"].as<std::string>());
    if (!status) return usage("未知结合状态: " + r["status"].as<std::string>());

    Union u;
    u.type = *type;
    u.status = *status;
    if (r.count("sequence")) u.sequence = r["sequence"].as<int>();
    if (r.count("place")) u.place_id = r["place"].as<std::string>();
    if (r.count("note")) u.note = r["note"].as<std::string>();
    if (r.count("start")) {
      auto d = parse_date_arg(r["start"].as<std::string>());
      if (!d) {
        print_error(d.error());
        return exit_code_for(d.error());
      }
      u.start_date = *d;
    }
    if (r.count("end")) {
      auto d = parse_date_arg(r["end"].as<std::string>());
      if (!d) {
        print_error(d.error());
        return exit_code_for(d.error());
      }
      u.end_date = *d;
    }
    if (r.count("member")) {
      auto members = parse_members(r["member"].as<std::string>());
      if (!members) {
        print_error(members.error());
        return exit_code_for(members.error());
      }
      u.members = *members;
    }
    return print_one(svc.create_union(u), format, print_union_text);
  }

  if (sub == "list") {
    cxxopts::Options opts("genea union list", "结合列表");
    opts.add_options()
      ("person", "按参与人物筛选", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("cursor", "游标", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    UnionFilter f;
    if (r.count("person")) f.person_id = r["person"].as<std::string>();
    if (r.count("cursor")) f.cursor = r["cursor"].as<std::string>();
    f.limit = r["limit"].as<int>();
    return print_list(svc.list_unions(f), format, print_union_text);
  }

  if (sub == "show" || sub == "remove") {
    cxxopts::Options opts("genea union " + sub, "结合详情/删除");
    opts.add_options()("id", "结合 ID", cxxopts::value<std::string>());
    opts.parse_positional({"id"});
    const auto r = parse_args(opts, rest);
    if (!r.count("id")) return usage("genea union " + sub + " <id>");
    const std::string id = r["id"].as<std::string>();
    if (sub == "show") {
      return print_one(svc.get_union(id), format, print_union_text);
    }
    auto removed = svc.remove_union(id);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"removed", id}}.dump(2) << "\n";
    } else {
      std::cout << "已删除: " << id << "\n";
    }
    return 0;
  }

  return usage("未知子命令: union " + sub +
               "（支持: add|list|show|remove）");
}

// ---------- event ----------

int cmd_event(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format) {
  if (args.empty()) return usage("genea event <add|list|show|remove> ...");
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  EventService svc(*db);

  if (sub == "add") {
    cxxopts::Options opts("genea event add", "新增事件");
    opts.add_options()
      ("subject-type", "person|union|branch|lineage",
       cxxopts::value<std::string>()->default_value("person"))
      ("subject", "主体 ID", cxxopts::value<std::string>())
      ("type", "birth|death|burial|marriage|migration|examination|"
               "office|honor|residence|annotation",
       cxxopts::value<std::string>()->default_value("annotation"))
      ("date", "事件日期：ISO 或老谱原文", cxxopts::value<std::string>())
      ("place", "地点 ID", cxxopts::value<std::string>())
      ("description", "事件描述", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);

    auto subject_type =
        event_subject_from_string(r["subject-type"].as<std::string>());
    if (!subject_type) {
      return usage("未知主体类型: " + r["subject-type"].as<std::string>());
    }
    auto type = event_type_from_string(r["type"].as<std::string>());
    if (!type) return usage("未知事件类型: " + r["type"].as<std::string>());

    Event e;
    e.subject_type = *subject_type;
    e.type = *type;
    if (r.count("subject")) e.subject_id = r["subject"].as<std::string>();
    if (r.count("place")) e.place_id = r["place"].as<std::string>();
    if (r.count("description"))
      e.description = r["description"].as<std::string>();
    if (r.count("date")) {
      auto d = parse_date_arg(r["date"].as<std::string>());
      if (!d) {
        print_error(d.error());
        return exit_code_for(d.error());
      }
      e.date = *d;
    }
    return print_one(svc.create_event(e), format, print_event_text);
  }

  if (sub == "list") {
    cxxopts::Options opts("genea event list", "事件列表");
    opts.add_options()
      ("subject-type", "按主体类型筛选", cxxopts::value<std::string>())
      ("subject", "按主体筛选", cxxopts::value<std::string>())
      ("type", "按事件类型筛选", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"))
      ("cursor", "游标", cxxopts::value<std::string>());
    const auto r = parse_args(opts, rest);
    EventFilter f;
    if (r.count("subject-type")) {
      auto st = event_subject_from_string(r["subject-type"].as<std::string>());
      if (!st) {
        return usage("未知主体类型: " + r["subject-type"].as<std::string>());
      }
      f.subject_type = *st;
    }
    if (r.count("subject")) f.subject_id = r["subject"].as<std::string>();
    if (r.count("type")) {
      auto t = event_type_from_string(r["type"].as<std::string>());
      if (!t) return usage("未知事件类型: " + r["type"].as<std::string>());
      f.type = *t;
    }
    if (r.count("cursor")) f.cursor = r["cursor"].as<std::string>();
    f.limit = r["limit"].as<int>();
    return print_list(svc.list_events(f), format, print_event_text);
  }

  if (sub == "show" || sub == "remove") {
    cxxopts::Options opts("genea event " + sub, "事件详情/删除");
    opts.add_options()("id", "事件 ID", cxxopts::value<std::string>());
    opts.parse_positional({"id"});
    const auto r = parse_args(opts, rest);
    if (!r.count("id")) return usage("genea event " + sub + " <id>");
    const std::string id = r["id"].as<std::string>();
    if (sub == "show") {
      return print_one(svc.get_event(id), format, print_event_text);
    }
    auto removed = svc.remove_event(id);
    if (!removed) {
      print_error(removed.error());
      return exit_code_for(removed.error());
    }
    if (format == "json") {
      std::cout << nlohmann::json{{"removed", id}}.dump(2) << "\n";
    } else {
      std::cout << "已删除: " << id << "\n";
    }
    return 0;
  }

  return usage("未知子命令: event " + sub +
               "（支持: add|list|show|remove）");
}

}  // namespace genea_cli
