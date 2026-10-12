// genea 命令行共享工具：错误码映射、cxxopts 适配、开库、日期参数解析。
// main.cpp 与 cmd_entity.cpp 共用，避免每个命令文件复制一遍。
#pragma once

#include <cxxopts.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>

namespace genea_cli {

// 退出码：0 成功 / 1 运行时错误 / 2 用法错误 / 3 不存在 / 4 版本冲突。
inline int exit_code_for(const genealogy::Error& err) {
  using genealogy::ErrorCode;
  switch (err.code()) {
    case ErrorCode::InvalidArgument: return 2;
    case ErrorCode::NotFound: return 3;
    case ErrorCode::Conflict: return 4;
    default: return 1;
  }
}

inline void print_error(const genealogy::Error& err) {
  std::cerr << "错误[" << static_cast<int>(err.code()) << "]: " << err.message()
            << "\n";
}

// cxxopts 3.x 仅提供 parse(argc, argv) 重载，且解析从下标 1 开始
// （argv[0] 被当作程序名跳过）。此处把参数向量转过去，并补一个
// 程序名占位，否则第一个参数会被静默吞掉。
// 指针指向 args 中的字符串与静态字面量，生命周期覆盖本次 parse 调用。
inline cxxopts::ParseResult parse_args(cxxopts::Options& opts,
                                       const std::vector<std::string>& args) {
  std::vector<const char*> argv;
  argv.push_back("genea");  // argv[0]：程序名占位
  argv.reserve(args.size() + 1);
  for (const auto& a : args) argv.push_back(a.c_str());
  int argc = static_cast<int>(argv.size());
  return opts.parse(argc, argv.data());
}

// 打开数据库并确保默认宗族占位（M1.2 上线 lineage 管理前，person add
// 未显式指定 --lineage 时落入该占位宗族）。
inline genealogy::Result<genealogy::Database> open_db(
    const std::string& path) {
  auto db = genealogy::Database::open(path);
  if (db) genealogy::ensure_default_lineage(*db);
  return db;
}

// 出生/卒年解析：先试 ISO，失败则按老谱原文照录。
inline genealogy::Result<genealogy::GenealogyDate> parse_date_arg(
    const std::string& text) {
  if (auto iso = genealogy::GenealogyDate::from_iso(text); iso) return iso;
  return genealogy::GenealogyDate::from_raw(text);
}

}  // namespace genea_cli
