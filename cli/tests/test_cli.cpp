// CLI 门禁测试：命令分派与帮助文本一致性。
// 历史上 7 处编译错误（字符串内嵌换行、未声明变量、缺失服务调用）
// 从未被任何目标编译拦截——cli 此前没有测试目标。本文件把 main.cpp
// 的 run() 编进测试二进制，任何翻译单元的编译错误都会让构建变红。

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

// main.cpp 中 run() 具外部链接（匿名命名空间于 :954 结束），此处声明。
int run(std::vector<std::string> raw_args);

TEST_CASE("CLI：帮助命令可达且退出码 0") {
  const int rc = run({"--help"});
  REQUIRE(rc == 0);
}

TEST_CASE("CLI：未知命令返回用法错误 2") {
  const int rc = run({"definitely-not-a-command"});
  REQUIRE(rc == 2);
}

TEST_CASE("CLI：date 命令不依赖数据库") {
  const int rc = run({"date", "convert", "2024-01-01"});
  REQUIRE(rc == 0);
}
