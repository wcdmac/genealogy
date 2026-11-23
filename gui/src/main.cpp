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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <genealogy/gui/app.hpp>

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Args {
  genealogy::gui::AppOptions app;
  bool headless_check = false;
};

Args parse_args(int argc, char** argv) {
  Args a;
  // 带范围校验的数值参数解析（第三轮新-C7）：atoi/atof 对垃圾输入
  // 返回 0 且不报错——0 高度的窗口起不来还不知道原因；未知参数此前
  // 被静默吞掉，拼写错误的参数（如 --widht）看起来「生效了」。
  auto parse_num = [](const char* flag, const std::string& text,
                      double lo, double hi) -> double {
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    // 第五轮 中-2：NaN 的比较恒 false 会绕过范围校验，随后
    // static_cast<int>(NaN) 是 UB（x86 常得 INT_MIN，窗口起不来）。
    if (text.empty() || end == text.c_str() || *end != '\0' ||
        !std::isfinite(v) || v < lo || v > hi) {
      std::fprintf(stderr, "%s 应为 %.0f..%.0f 的数字，得到: %s\n", flag, lo,
                   hi, text.c_str());
      std::exit(3);
    }
    return v;
  };
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "参数 %s 缺值\n", k.c_str());
        std::exit(3);
      }
      return argv[++i];
    };
    if (k == "--db") {
      a.app.db_path = next();
    } else if (k == "--width") {
      a.app.width = static_cast<int>(parse_num("--width", next(), 320, 8192));
    } else if (k == "--height") {
      a.app.height = static_cast<int>(parse_num("--height", next(), 240, 8192));
    } else if (k == "--font-size") {
      a.app.font_size =
          static_cast<float>(parse_num("--font-size", next(), 8, 72));
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
    } else {
      std::fprintf(stderr, "未知参数: %s（--help 看用法）\n", k.c_str());
      std::exit(3);
    }
  }
  return a;
}

}  // namespace

// 入口共用体：Windows GUI 子系统用 WinMain 调它，其余平台直接 main。
int gui_main(int argc, char** argv) {
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


#if defined(_WIN32)
// WIN32 子系统不再自动附带控制台黑框；命令行参数用 GetCommandLineW +
// CommandLineToArgvW 取（UTF-16 → UTF-8，与 CLI 的 wmain 同口径）。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t* wide = nullptr;
  LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv_w == nullptr) return 1;
  std::vector<std::string> utf8;
  utf8.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) { utf8.emplace_back(); continue; }
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, s.data(), n, nullptr, nullptr);
    utf8.push_back(std::move(s));
  }
  LocalFree(argv_w);
  std::vector<char*> argv;
  argv.reserve(utf8.size());
  for (auto& s : utf8) argv.push_back(s.data());
  return gui_main(static_cast<int>(argv.size()), argv.data());
}
#else
int main(int argc, char** argv) { return gui_main(argc, argv); }
#endif
