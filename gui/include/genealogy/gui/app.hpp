#pragma once

// 应用主体：GLFW 窗口 + OpenGL 3.2+ + ImGui(docking) 主循环。
//
// 职责边界：app 只管「窗口、字体、主题、菜单、面板注册、主循环」，
// 不管任何业务——业务全在面板里，面板只经 DbSession 调 core（ADR-1）。
//
// 无头环境：GLFW 在没有显示服务器的机器上 glfwInit() 直接失败。
// 此时 run() 返回 false 并在 err 里说明，main 负责转成人类可读的
// 提示并以非零码退出——CI 的 Linux runner 无显示，不能让它看起来像
// 「GUI 崩了」，而是「此环境无法开窗」。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/db_session.hpp>
#include <genealogy/gui/panels.hpp>

#include <memory>
#include <string>
#include <vector>

namespace genealogy::gui {

struct AppOptions {
  std::string db_path;    // 启动时打开的库（空 = 打开时不载库）
  int width = 1600;
  int height = 1000;
  float font_size = 16.0F;   // 高 DPI 下由 style.ScaleAllSizes 统一缩放
  bool high_contrast = false;  // 高对比度皮肤（设计文档 6.4）
};

class GenealogyApp {
 public:
  explicit GenealogyApp(AppOptions options);

  // 初始化 + 主循环。成功返回 true；窗口开不起来时 false + err。
  bool run(std::string* err);

 private:
  void build_panels();
  void draw_menu_bar();
  void draw_status_bar();
  // 换库/关库的统一清理序列（db_switch.cpp，可无头单测）——菜单与
  // 打开弹窗两个入口共用
  void reset_panels_for_switch();

  AppOptions options_;
  AppState state_;
  DbSession session_;
  std::vector<std::unique_ptr<Panel>> panels_;
  bool want_quit_ = false;
  bool show_open_dialog_ = false;   // 菜单置位，主帧层级开弹窗
  char path_buf_[512] = {};
};

}  // namespace genealogy::gui
