// genealogy-gui：桌面 GUI 入口（Dear ImGui + GLFW）。
//
// 用法：
//   genealogy-gui [--db <path>] [--width N] [--height N]
//                 [--font-size PX] [--high-contrast] [--headless-check]
//
// --headless-check：只验证「此环境能不能开窗」，然后退出。
// 给 CI / SSH 会话用：退出码 0 = 环境可用，2 = 无显示环境，
// 1 = 其他故障。这样「CI 上没显示器」与「GUI 真的崩了」可区分——
// 前者是环境事实，后者是缺陷。

#include <genealogy/gui/app.hpp>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct Args {
  genealogy::gui::AppOptions app;
  bool headless_check = false;
};

Args parse_args(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&]() -> std::string {
      return i + 1 < argc ? argv[++i] : std::string{};
    };
    if (k == "--db") {
      a.app.db_path = next();
    } else if (k == "--width") {
      a.app.width = std::atoi(next().c_str());
    } else if (k == "--height") {
      a.app.height = std::atoi(next().c_str());
    } else if (k == "--font-size") {
      a.app.font_size = static_cast<float>(std::atof(next().c_str()));
    } else if (k == "--high-contrast") {
      a.app.high_contrast = true;
    } else if (k == "--headless-check") {
      a.headless_check = true;
    } else if (k == "--help" || k == "-h") {
      std::printf(
          "用法: genealogy-gui [--db <path>] [--width N] [--height N]\n"
          "                     [--font-size PX] [--high-contrast]\n"
          "                     [--headless-check]\n");
      std::exit(0);
    }
  }
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);

  if (args.headless_check) {
    // 只探环境：glfwInit 在无显示服务器的机器上直接失败。
    if (glfwInit() != GLFW_TRUE) {
      const char* desc = nullptr;
      glfwGetError(&desc);
      std::fprintf(stderr, "headless: 无法初始化 GLFW（%s）\n",
                   desc != nullptr ? desc : "未知原因");
      glfwTerminate();
      return 2;   // 环境无显示——不是缺陷
    }
    glfwTerminate();
    std::printf("headless: 显示环境可用\n");
    return 0;
  }

  genealogy::gui::GenealogyApp app(args.app);
  std::string err;
  if (!app.run(&err)) {
    std::fprintf(stderr, "启动失败: %s\n", err.c_str());
    return 1;
  }
  return 0;
}
