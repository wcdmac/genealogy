// 迁徙命令组（M5-4 补 CLI：CLI/REST/Web 三端共用 core 用例层，ADR-1）。
//
// 为什么 CLI 也要有：REST + Web UI 之外的「脚本与运维」入口——灌库后
// 批量补迁徙、CI 校验脚本、无人值守运维都走 CLI。少一端，那端的需求
// 就会以「直接拼 HTTP」的形式冒出来，反而更脏。

#include "cmd_migration.hpp"

#include <cxxopts.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/migration.hpp>
#include <genealogy/service/migration_service.hpp>
#include <genealogy/service/place_service.hpp>

#include "cli_util.hpp"

// 与 cmd_entity/cmd_io 同一惯例：命令组文件里 using namespace genealogy，
// 模型/仓储/服务类型直接写短名（Person/MigrationService…）。
using namespace genealogy;

namespace genea_cli {
namespace {

void print_migration_text(const Migration& m) {
  std::cout << m.id << "\t" << to_string(m.subject_type) << "/"
            << m.subject_id << "\t"
            << (m.from_place_id ? *m.from_place_id : "-") << " → "
            << (m.to_place_id ? *m.to_place_id : "-");
  if (m.reason) std::cout << "\t" << *m.reason;
  std::cout << "\n";
}

}  // namespace

int cmd_migration(const std::vector<std::string>& args,
                  const std::string& db_path, const std::string& format) {
  if (args.empty()) {
    std::cerr << "用法错误: genea migration add|list|show|remove\n";
    return 2;
  }
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  cxxopts::Options opts("genea migration " + sub, "迁徙" + sub);
  opts.add_options()
      ("subject-type", "主体类型: person|branch", cxxopts::value<std::string>())
      ("subject", "主体 ID", cxxopts::value<std::string>())
      ("from", "迁出地 ID（可空）", cxxopts::value<std::string>())
      ("to", "迁入地 ID", cxxopts::value<std::string>())
      ("reason", "迁因（经商/避乱/仕进/投亲…）", cxxopts::value<std::string>())
      ("route", "迁经路线（文字描述）", cxxopts::value<std::string>())
      ("date", "迁徙日期：老谱原文或 ISO", cxxopts::value<std::string>())
      ("limit", "每页条数", cxxopts::value<int>()->default_value("50"));
  const auto r = parse_args(opts, rest);

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  MigrationService svc(*db);

  if (sub == "add") {
    if (!r.count("subject-type") || !r.count("subject") || !r.count("to")) {
      std::cerr << "用法错误: genea migration add --subject-type person|branch"
                   " --subject <ID> --to <地点ID> [--from <地点ID>]\n";
      return 2;
    }
    Migration m;
    const auto type = migration_subject_from_string(r["subject-type"].as<std::string>());
    if (!type) {
      std::cerr << "用法错误: --subject-type 应为 person 或 branch\n";
      return 2;
    }
    m.subject_type = *type;
    m.subject_id = r["subject"].as<std::string>();
    m.to_place_id = r["to"].as<std::string>();
    if (r.count("from")) m.from_place_id = r["from"].as<std::string>();
    if (r.count("reason")) m.reason = r["reason"].as<std::string>();
    if (r.count("route")) m.route = r["route"].as<std::string>();
    if (r.count("date")) {
      // 谱面原文照录（与 event/union 同口径）：解不出公历就只存原文，
      // 不编日期。ISO 输入由 GenealogyDate::from_iso 校验。
      const std::string raw = r["date"].as<std::string>();
      auto parsed = GenealogyDate::from_raw(raw);
      // 解析失败显式告警（第五轮 P3-2）：此前静默丢弃，日期悄悄消失。
      if (parsed) {
        m.date = *parsed;
      } else {
        std::cerr << "警告: --date " + raw + " 无法解析，本次建档不带日期" << "\n";
      }
    }
    auto made = svc.create_migration(m);
    if (!made) {
      print_error(made.error());
      return exit_code_for(made.error());
    }
    if (format == "json") {
      std::cout << to_json(*made).dump(2) << "\n";
    } else {
      std::cout << "已登记迁徙: " << made->id << "\n";
    }
    return 0;
  }

  if (sub == "list") {
    MigrationFilter f;
    f.limit = r.count("limit") ? r["limit"].as<int>() : 50;
    if (r.count("subject")) f.subject_id = r["subject"].as<std::string>();
    if (r.count("subject-type")) {
      const auto type = migration_subject_from_string(r["subject-type"].as<std::string>());
      if (!type) {
        std::cerr << "用法错误: --subject-type 应为 person 或 branch\n";
        return 2;
      }
      f.subject_type = *type;
    }
    auto rows = svc.list_migrations(f);
    if (!rows) {
      print_error(rows.error());
      return exit_code_for(rows.error());
    }
    if (format == "json") {
      nlohmann::json items = nlohmann::json::array();
      for (const auto& m : *rows) items.push_back(to_json(m));
      std::cout << items.dump(2) << "\n";
    } else {
      for (const auto& m : *rows) print_migration_text(m);
      if (rows->empty()) std::cout << "（无迁徙记录）\n";
    }
    return 0;
  }

  if (sub == "show") {
    if (rest.empty()) {
      std::cerr << "用法错误: genea migration show <ID>\n";
      return 2;
    }
    auto got = svc.get_migration(rest[0]);
    if (!got) {
      print_error(got.error());
      return exit_code_for(got.error());
    }
    if (format == "json") {
      std::cout << to_json(*got).dump(2) << "\n";
    } else {
      print_migration_text(*got);
    }
    return 0;
  }

  if (sub == "remove") {
    if (rest.empty()) {
      std::cerr << "用法错误: genea migration remove <ID>\n";
      return 2;
    }
    auto gone = svc.remove_migration(rest[0]);
    if (!gone) {
      print_error(gone.error());
      return exit_code_for(gone.error());
    }
    if (format == "json") {
      nlohmann::json out{{"removed", rest[0]}, {"status", "ok"}};
      std::cout << out.dump(2) << "\n";
    } else {
      std::cout << "已删除迁徙: " << rest[0] << "\n";
    }
    return 0;
  }

  std::cerr << "用法错误: genea migration add|list|show|remove\n";
  return 2;
}

}  // namespace genea_cli
