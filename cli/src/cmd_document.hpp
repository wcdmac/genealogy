// 文档命令组（M7.4 谱序/凡例/族规家训/艺文/契约，三端共用 core 用例层）。
//
//   genea document add --type preface --title "嘉庆四年修谱序"
//                     [--author 陈文远] [--date 嘉庆四年] [--edition 嘉庆四年修]
//                     [--content 正文全文] [--seq 1]
//   genea document list [--type preface]
//   genea document show <id>
//   genea document search --query 正文片段   # trigram FTS 中文子串命中，
//                                        # 1-2 字查询回退 LIKE（验收门）
//   genea document remove <id>
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

int cmd_document(const std::vector<std::string>& args,
                 const std::string& db_path, const std::string& format);

}  // namespace genea_cli
