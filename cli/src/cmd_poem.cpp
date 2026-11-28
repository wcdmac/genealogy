#include "cmd_poem.hpp"

#include "cli_util.hpp"

#include <cxxopts.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/generation_poem.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/generation_poem_service.hpp>

namespace genea_cli {
namespace {

using namespace genealogy;

int usage() {
  std::cerr << "用法: genea poem <set|show|check> [--lineage ID|--branch ID]\n"
            << "  set   --text \"联句\"   录入/更换派语\n"
            << "  show                 查看辈字序列\n"
            << "  check [--start N]    辈字与世代交叉校验（N=派语首句世代，默认 1）\n";
  return 2;
}

// 归属解析：--lineage 与 --branch 二选一，都给或都不给都是用法错误。
// 返回 false 时已打印错误，调用方直接返回退出码。
bool resolve_owner(const cxxopts::ParseResult& r, PoemOwner* owner,
                   std::string* owner_id) {
  const bool has_lineage = r.count("lineage") > 0;
  const bool has_branch = r.count("branch") > 0;
  // 单谱单库：缺省即谱级派语（owner_id 固定 "book"，与 0007 的
  // generation_poem 谱级归一一致）。--lineage 保留作向后兼容别名，
  // 与 --branch 同时给出仍视为用法错误。
  if (has_lineage && has_branch) {
    std::cerr << "用法错误: --lineage 与 --branch 不能同时给出\n";
    return false;
  }
  if (has_branch) {
    *owner = PoemOwner::Branch;
    *owner_id = r["branch"].as<std::string>();
  } else {
    *owner = PoemOwner::Lineage;
    *owner_id = has_lineage ? r["lineage"].as<std::string>() : std::string("book");
  }
  return true;
}

void print_entry_text(const GenerationPoemEntry& e) {
  std::cout << "第 " << e.seq << " 世\t" << e.character;
  if (e.note) std::cout << "\t（" << *e.note << "）";
  std::cout << "\n";
}

}  // namespace

int cmd_poem(const std::vector<std::string>& args, const std::string& db_path,
             const std::string& format) {
  if (args.empty()) return usage();
  const std::string sub = args[0];
  const std::vector<std::string> rest(args.begin() + 1, args.end());

  cxxopts::Options opts("genea poem " + sub, "派语（字辈）管理");
  opts.add_options()
    ("lineage", "宗族 ID", cxxopts::value<std::string>())
    ("branch", "支派 ID", cxxopts::value<std::string>())
    ("text", "派语联句原文", cxxopts::value<std::string>())
    ("start", "派语首句对应的世代序号", cxxopts::value<int>()->default_value("1"))
    ("help", "显示用法");
  const auto r = parse_args(opts, rest);
  if (r.count("help") > 0) return usage();

  PoemOwner owner = PoemOwner::Lineage;
  std::string owner_id;
  if (!resolve_owner(r, &owner, &owner_id)) return 2;

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }
  GenerationPoemService svc(*db);

  if (sub == "set") {
    if (r.count("text") == 0) {
      std::cerr << "用法错误: set 需要 --text \"联句\"\n";
      return 2;
    }
    const auto set = svc.set_poem(owner, owner_id, r["text"].as<std::string>());
    if (!set) {
      print_error(set.error());
      return exit_code_for(set.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& e : *set) arr.push_back(to_json(e));
      std::cout << arr.dump(2) << "\n";
    } else {
      std::cout << "已录入 " << set->size() << " 个辈字:\n";
      for (const auto& e : *set) print_entry_text(e);
    }
    return 0;
  }

  if (sub == "show") {
    const auto got = svc.get_poem(owner, owner_id);
    if (!got) {
      print_error(got.error());
      return exit_code_for(got.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& e : *got) arr.push_back(to_json(e));
      std::cout << arr.dump(2) << "\n";
      return 0;
    }
    if (got->empty()) {
      std::cout << "（该归属尚未录入派语）\n";
      return 0;
    }
    for (const auto& e : *got) print_entry_text(e);
    return 0;
  }

  if (sub == "check") {
    const int start = r["start"].as<int>();
    // 范围守卫（第五轮 P3-10）：负数/超大值会让 expected 计算满屏假冲突
    if (start < 1) {
      std::cerr << "用法错误: --start 至少为 1" << "\n";
      return 2;
    }
    const auto conflicts =
        svc.check_consistency(owner, owner_id, start);
    if (!conflicts) {
      print_error(conflicts.error());
      return exit_code_for(conflicts.error());
    }
    if (format == "json") {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& c : *conflicts) {
        arr.push_back({{"person_id", c.person_id},
                       {"person_name", c.person_name},
                       {"name_seq", c.name_seq.value_or(0)},
                       {"generation_no", c.generation_no.value_or(0)},
                       {"expected_no", c.expected_no.value_or(0)},
                       {"detail", c.detail}});
      }
      std::cout << arr.dump(2) << "\n";
    } else if (conflicts->empty()) {
      std::cout << "辈字与世代一致，未发现冲突\n";
    } else {
      std::cout << "发现 " << conflicts->size() << " 处冲突:\n";
      for (const auto& c : *conflicts) {
        std::cout << "  " << c.person_name << "（" << c.person_id << "）: "
                  << c.detail << "\n";
      }
    }
    // 有冲突返回 1：与 io validate 同口径，可当 CI 卡点
    return conflicts->empty() ? 0 : 1;
  }

  return usage();
}

}  // namespace genea_cli
