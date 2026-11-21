// 备份 / 恢复命令（M6-3：发行前的「备份恢复演练」验收项）。
//
// 备份 = SQLite 官方快照 VACUUM INTO（一致性 + 单文件 + 顺带压缩），
// 不是文件拷贝——正在写入的库直接 cp 会拷到半提交状态（WAL 未合并），
// 那种备份恢复出来可能是坏的。VACUUM INTO 在连接内做一致性读，
// 不阻塞其他连接（SQLite 3.27+）。
//
// 附件库（attachments/）随库走：谱数据是根，遗像扫描件是叶，
// 只恢复库不恢复图，恢复出来的谱是「缺画像的谱」——所以一起打。

#include "cmd_backup.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <cxxopts.hpp>
#include <sqlite3.h>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/ulid.hpp>

#include "cli_util.hpp"

using genealogy::Error;
using genealogy::ErrorCode;

namespace genea_cli {
namespace {

namespace fs = std::filesystem;

// 备份目录名/快照文件名的时间戳（本地时间，秒级）。
std::string timestamp_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &seconds);
#else
  localtime_r(&seconds, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%S", &tm);
  return buffer;
}

// SQL 字符串字面量转义：单引号翻倍。路径来自命令行，不转义会把
// 用户的引号变成 SQL 语法（也是注入面）。
std::string sql_quote(const std::string& path) {
  std::string out = "'";
  for (const char c : path) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

std::string human_size(std::uintmax_t bytes) {
  char buffer[32];
  if (bytes >= 1024 * 1024) {
    std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                  static_cast<double>(bytes) / (1024 * 1024));
  } else if (bytes >= 1024) {
    std::snprintf(buffer, sizeof(buffer), "%.1f KB",
                  static_cast<double>(bytes) / 1024);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu B",
                  static_cast<unsigned long long>(bytes));
  }
  return buffer;
}

// 递归拷贝目录（不存在则跳过，返回是否拷了东西）。
bool copy_tree_if_exists(const fs::path& from, const fs::path& to,
                         std::string* why) {
  std::error_code ec;
  if (!fs::exists(from, ec)) return false;
  fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) {
    *why = ec.message();
    return false;
  }
  return true;
}

}  // namespace

int cmd_db_backup(const std::vector<std::string>& args,
                  const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea db backup", "一致性快照备份（VACUUM INTO）");
  opts.add_options()
      ("out", "备份输出目录", cxxopts::value<std::string>());
  const auto r = parse_args(opts, args);
  const std::string out_dir = r.count("out") > 0 ? r["out"].as<std::string>() : "";
  if (out_dir.empty()) {
    std::cerr << "用法错误: db backup 缺 --out <目录>\n";
    return 2;
  }

  auto db = open_db(db_path);
  if (!db) {
    print_error(db.error());
    return exit_code_for(db.error());
  }

  std::error_code ec;
  const fs::path out(out_dir);
  fs::create_directories(out, ec);
  if (ec) {
    print_error(Error{ErrorCode::IoError, "建不了备份目录: " + ec.message()});
    return 1;
  }

  const std::string stamp = timestamp_now();
  const fs::path snapshot = out / ("genealogy-" + stamp + ".db");

  // VACUUM INTO：一致性快照。失败带 sqlite 原始信息（目标目录
  // 不可写、磁盘满都在这里暴露）。
  const std::string sql = "VACUUM INTO " + sql_quote(snapshot.string());
  char* errmsg = nullptr;
  if (sqlite3_exec(db->raw(), sql.c_str(), nullptr, nullptr, &errmsg) !=
      SQLITE_OK) {
    const std::string message = errmsg ? errmsg : "未知错误";
    sqlite3_free(errmsg);
    print_error(Error{ErrorCode::IoError, "快照失败: " + message});
    return 1;
  }

  // 附件库随库走（存在才拷）
  const fs::path attachments = fs::path(db_path).parent_path() / "attachments";
  std::string copy_why;
  const bool has_attachments =
      copy_tree_if_exists(attachments, out / "attachments", &copy_why);
  if (!copy_why.empty()) {
    print_error(Error{ErrorCode::IoError, "附件库备份失败: " + copy_why});
    return 1;
  }

  if (format == "json") {
    nlohmann::json out_json{{"snapshot", snapshot.string()},
                            {"bytes", fs::file_size(snapshot, ec)},
                            {"attachments", has_attachments},
                            {"status", "ok"}};
    std::cout << out_json.dump(2) << "\n";
  } else {
    std::cout << "备份完成: " << snapshot.string() << "（"
              << human_size(fs::file_size(snapshot, ec)) << "）\n";
    if (has_attachments) std::cout << "附件库: " << (out / "attachments").string() << "\n";
    std::cout << "恢复: genea db restore --from " << out.string()
              << " --db <目标库>\n";
  }
  return 0;
}

int cmd_db_restore(const std::vector<std::string>& args,
                   const std::string& db_path, const std::string& format) {
  cxxopts::Options opts("genea db restore", "从备份目录恢复");
  opts.add_options()
      ("from", "备份目录（genea db backup 的 --out）", cxxopts::value<std::string>())
      ("force", "目标库已存在时强制恢复（旧库移开为 .bak，不删除）",
       cxxopts::value<bool>()->default_value("false")->implicit_value("true"));
  const auto r = parse_args(opts, args);
  const std::string from_dir =
      r.count("from") > 0 ? r["from"].as<std::string>() : "";
  if (from_dir.empty()) {
    std::cerr << "用法错误: db restore 缺 --from <目录>\n";
    return 2;
  }
  const bool force = r.count("force") > 0 && r["force"].as<bool>();

  std::error_code ec;
  const fs::path from(from_dir);

  // 顺序纪律（2026-11 静态审查 H2/H3）：先验证备份目录与快照、再动
  // 活库——此前 --force 先把活库移开成 .bak、打印成功，然后才发现
  // --from 写错，旧库已被搬走、新库没落地。恢复 = 破坏性操作，
  // 所有前置校验必须先行。
  if (!fs::is_directory(from, ec)) {
    print_error(Error{ErrorCode::InvalidArgument, "备份目录不存在: " + from_dir});
    return 2;
  }

  // 目录里取最新的快照（时间戳文件名字典序即时间序）。
  fs::path snapshot;
  std::filesystem::file_time_type newest{};
  bool found = false;
  for (const auto& entry : fs::directory_iterator(from, ec)) {
    if (!entry.is_regular_file()) continue;
    const auto& path = entry.path();
    if (path.extension() != ".db") continue;
    const auto modified = fs::last_write_time(path, ec);
    if (!found || modified > newest) {
      snapshot = path;
      newest = modified;
      found = true;
    }
  }
  if (ec) {
    print_error(Error{ErrorCode::IoError, "读备份目录失败: " + ec.message()});
    return 1;
  }
  if (!found) {
    print_error(Error{ErrorCode::InvalidArgument,
                      "备份目录里没有 .db 快照: " + from_dir});
    return 2;
  }

  // 目标已存在且未 force：拒绝。拿备份盖活库 = 用昨天盖今天，
  // 这种事故必须让人显式确认。
  if (fs::exists(db_path, ec) && !force) {
    print_error(Error{ErrorCode::Conflict,
                      "目标库已存在: " + db_path +
                          "（确认要覆盖请加 --force，旧库会移开为 .bak）"});
    return 4;
  }

  // --force：旧库连同 -wal/-shm 旁车一起移开（H3——只搬主文件的话，
  // 残留的旧 WAL 会在新库首次打开时被重放，恢复出的数据被旧事务污染）。
  fs::path moved_main, moved_wal, moved_shm;
  bool moved = false;
  // 补偿：把旧库（含旁车）移回原位。主库搬移之后的任何一步失败——
  // 旁车 rename、快照拷贝——都必须走这里，否则旧库滞留 .bak、新库未
  // 落地，用户库"凭空消失"（复核 2.6：此前仅拷贝失败分支有补偿，
  // 旁车 rename 失败直接 return 1，无回滚）。
  auto rollback_moved = [&]() {
    if (!moved) return;
    std::error_code rec;
    fs::rename(moved_main, db_path, rec);
    for (const auto& [src, dst] :
         {std::pair{moved_wal, fs::path(db_path).string() + "-wal"},
          {moved_shm, fs::path(db_path).string() + "-shm"}}) {
      std::error_code ec4;
      if (fs::exists(src, ec4)) fs::rename(src, dst, ec4);
    }
    if (!rec) {
      std::cout << "已回滚: 旧库从 " << moved_main.string() << " 移回原位\n";
    } else {
      std::cout << "回滚失败: 旧库留在 " << moved_main.string()
                << "，请手动移回原位\n";
    }
  };
  if (fs::exists(db_path, ec)) {
    const std::string stamp = ".bak-" + timestamp_now();
    moved_main = fs::path(db_path).string() + stamp;
    moved_wal = fs::path(db_path).string() + "-wal" + stamp;
    moved_shm = fs::path(db_path).string() + "-shm" + stamp;
    fs::rename(db_path, moved_main, ec);
    if (ec) {
      print_error(Error{ErrorCode::IoError, "移开旧库失败: " + ec.message()});
      return 1;
    }
    moved = true;
    for (const auto& [src, dst] :
         {std::pair{fs::path(db_path).string() + "-wal", moved_wal},
          {fs::path(db_path).string() + "-shm", moved_shm}}) {
      std::error_code ec2;
      if (fs::exists(src, ec2)) {
        fs::rename(src, dst, ec2);
        if (ec2) {
          print_error(Error{ErrorCode::IoError,
                            "移开旧库旁车文件失败: " + ec2.message()});
          rollback_moved();
          return 1;
        }
      }
    }
    std::cout << "旧库已移开: " << moved_main.string() << "\n";
  }

  fs::copy_file(snapshot, db_path, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    print_error(Error{ErrorCode::IoError,
                      "恢复失败: " + ec.message() + "（目标: " + db_path + "）"});
    if (moved) {
      // 补偿：新库没落地，把旧库移回原位——恢复失败不留两头都空的最坏态
      rollback_moved();
    }
    return 1;
  }

  const fs::path attachments_from = from / "attachments";
  const fs::path attachments_to = fs::path(db_path).parent_path() / "attachments";
  std::string copy_why;
  const bool restored_attachments =
      copy_tree_if_exists(attachments_from, attachments_to, &copy_why);
  if (!copy_why.empty()) {
    print_error(Error{ErrorCode::IoError, "附件库恢复失败: " + copy_why});
    return 1;
  }

  if (format == "json") {
    nlohmann::json out_json{{"restored", db_path},
                            {"from", snapshot.string()},
                            {"attachments", restored_attachments},
                            {"status", "ok"}};
    std::cout << out_json.dump(2) << "\n";
  } else {
    std::cout << "恢复完成: " << db_path << " ← " << snapshot.string() << "\n";
    if (restored_attachments) {
      std::cout << "附件库: " << attachments_to.string() << "\n";
    }
  }
  return 0;
}

}  // namespace genea_cli
