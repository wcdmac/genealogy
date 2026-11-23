// genea 导入导出与校验命令组入口声明。
#pragma once

#include <string>
#include <vector>

namespace genea_cli {

// args 已去掉命令组名，首元素为子命令（gedcom / validate）。
int cmd_io(const std::vector<std::string>& args, const std::string& db_path,
           const std::string& format);

// 世系树导出（DOT / Mermaid，渲染走 core io::render_tree）。
int cmd_export_tree(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format);

// P0 出版：谱书 HTML（浏览器打印成册）。
int cmd_export_book(const std::vector<std::string>& args,
                    const std::string& db_path, const std::string& format);

// 性能验收：灌库 + 跑基准（M1.6）。
int cmd_bench(const std::vector<std::string>& args, const std::string& db_path,
              const std::string& format);

// 合成谱灌库（M1.6 性能验收的数据来源）。
int cmd_db_seed(const std::vector<std::string>& args, const std::string& db_path,
                const std::string& format);

}  // namespace genea_cli
