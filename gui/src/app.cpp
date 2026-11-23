#include <genealogy/gui/app.hpp>

#include <genealogy/gui/db_switch.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <genealogy/gui/font.hpp>
#include <genealogy/gui/panels/person_card.hpp>
#include <genealogy/gui/panels/new_person_panel.hpp>
#include <genealogy/gui/panels/export_panel.hpp>
#include <genealogy/gui/panels/branch_panel.hpp>
#include <genealogy/gui/panels/place_panel.hpp>
#include <genealogy/gui/panels/poem_panel.hpp>
#include <genealogy/gui/panels/backup_panel.hpp>
#include <genealogy/gui/panels/person_table.hpp>
#include <genealogy/gui/panels/relation_panel.hpp>
#include <genealogy/gui/panels/search_panel.hpp>
#include <genealogy/gui/panels/tree_panel.hpp>
#include <genealogy/gui/panels/validation_panel.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace genealogy::gui {

namespace {

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "[glfw] error %d: %s\n", error, description);
}

// 主题：默认 ImGui Dark 为底，按高对比度选项加深对比。
// 设计文档 6.4 要求「高对比度皮肤 + 字体缩放」；一期给一个开关，
// 字号缩放走 ImGui::GetIO().FontGlobalScale（菜单里 +/- 调）。
void apply_theme(bool high_contrast) {
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  if (high_contrast) {
    style.Colors[ImGuiCol_Text] = ImVec4(1.00F, 1.00F, 1.00F, 1.00F);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.75F, 0.75F, 0.75F, 1.00F);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06F, 0.06F, 0.08F, 1.00F);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08F, 0.08F, 0.10F, 1.00F);
    style.Colors[ImGuiCol_Border] = ImVec4(0.70F, 0.70F, 0.75F, 1.00F);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16F, 0.16F, 0.20F, 1.00F);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.20F, 0.20F, 0.26F, 1.00F);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10F, 0.10F, 0.14F, 1.00F);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16F, 0.16F, 0.22F, 1.00F);
    style.Colors[ImGuiCol_Button] = ImVec4(0.22F, 0.22F, 0.30F, 1.00F);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.32F, 0.32F, 0.42F, 1.00F);
    style.Colors[ImGuiCol_Header] = ImVec4(0.26F, 0.26F, 0.38F, 1.00F);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.36F, 0.36F, 0.50F, 1.00F);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.55F, 0.78F, 1.00F, 1.00F);
  }
  style.WindowRounding = 2.0F;
  style.FrameRounding = 2.0F;
  style.ScrollbarRounding = 2.0F;
  style.ItemSpacing = ImVec2(8, 5);
}

}  // namespace

GenealogyApp::GenealogyApp(AppOptions options)
    : options_(std::move(options)) {
  std::snprintf(path_buf_, sizeof(path_buf_), "%s", options_.db_path.c_str());
}

void GenealogyApp::build_panels() {
  panels_.push_back(std::make_unique<PersonCardPanel>(state_));
  panels_.push_back(std::make_unique<NewPersonPanel>(state_));
  panels_.push_back(std::make_unique<ExportPanel>(state_));
  panels_.push_back(std::make_unique<BranchPanel>(state_));
  panels_.push_back(std::make_unique<PlacePanel>(state_));
  panels_.push_back(std::make_unique<PoemPanel>(state_));
  panels_.push_back(std::make_unique<BackupPanel>(state_));
  panels_.push_back(std::make_unique<PersonTablePanel>(state_));
  panels_.push_back(std::make_unique<SearchPanel>(state_));
  panels_.push_back(std::make_unique<RelationPanel>(state_));
  panels_.push_back(std::make_unique<TreePanel>(state_));
  panels_.push_back(std::make_unique<ValidationPanel>(state_));
}

void GenealogyApp::draw_menu_bar() {
  if (!ImGui::BeginMainMenuBar()) return;

  if (ImGui::BeginMenu("文件")) {
    if (ImGui::MenuItem("打开数据库…", "Ctrl+O")) {
      show_open_dialog_ = true;
    }
    if (ImGui::MenuItem("新建人物…", "Ctrl+N")) {
      if (session_.is_open() && panels_.size() > 1) panels_[1]->open = true;
      else state_.set_status("请先打开数据库", true);
    }
    if (ImGui::MenuItem("关闭数据库")) {
      // 先停各面板后台线程并清缓存，再析构 session：worker 持有
      // DbSession* 并在后台查询，反向顺序会 use-after-free（H-3）。
      // 与打开库路径（:269）使用同一份 reset_for_database_switch。
      reset_panels_for_switch();
      session_.close();
      state_.set_status("数据库已关闭");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("退出", "Alt+F4")) {
      want_quit_ = true;
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("面板")) {
    for (auto& p : panels_) {
      ImGui::MenuItem(p->title(), nullptr, &p->open);
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("视图")) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::MenuItem("字体放大", "+")) io.FontGlobalScale += 0.1F;
    if (ImGui::MenuItem("字体缩小", "-")) {
      io.FontGlobalScale = io.FontGlobalScale > 0.3F ? io.FontGlobalScale - 0.1F
                                                     : io.FontGlobalScale;
    }
    if (ImGui::MenuItem("重置缩放", nullptr, false,
                        io.FontGlobalScale != 1.0F)) {
      io.FontGlobalScale = 1.0F;
    }
    ImGui::Separator();
    ImGui::MenuItem("高对比度", nullptr, &options_.high_contrast);
    ImGui::EndMenu();
    // 主题每帧应用（ImGui 样式本就逐帧设置）：只翻 bool 不调用
    // apply_theme 曾让高对比度开关毫无视觉效果（2026-11 静态审查 L4）
    apply_theme(options_.high_contrast);
  }
  ImGui::EndMainMenuBar();

  // 快捷键：Ctrl+O 开库（菜单项只显示助记符，真正响应在这里）
  ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
    show_open_dialog_ = true;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false) &&
      session_.is_open() && panels_.size() > 1) {
    panels_[1]->open = true;
  }
}

void GenealogyApp::draw_status_bar() {
  // 底部固定窗口当状态栏。
  // 不用 BeginViewportSideBar：那个 API 不在 1.93 的头文件里
  // （是更早/更晚变体里的东西），用了直接编不过。
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = ImGui::GetFrameHeight();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus;
  if (ImGui::Begin("##status", nullptr, flags)) {
    const ImVec4 color = state_.status_is_error
                             ? ImVec4(1.0F, 0.45F, 0.45F, 1.0F)
                             : ImVec4(0.75F, 0.78F, 0.80F, 1.0F);
    ImGui::TextColored(color, "%s", state_.status_message.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| 库: %s",
                        session_.is_open() ? session_.path().c_str() : "（未开）");
    ImGui::SameLine();
    ImGui::TextDisabled("| 在世者详情: %s",
                        state_.show_living_details ? "显示" : "脱敏");
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void GenealogyApp::reset_panels_for_switch() {
  std::vector<Panel*> ptrs;
  ptrs.reserve(panels_.size());
  for (auto& p : panels_) ptrs.push_back(p.get());
  reset_for_database_switch(state_, ptrs);
}

bool GenealogyApp::run(std::string* err) {
  glfwSetErrorCallback(glfw_error_callback);
  if (glfwInit() != GLFW_TRUE) {
    if (err != nullptr) {
      *err = "GLFW 初始化失败（无显示服务器？此程序需要图形环境）。"
             "Windows/Linux/macOS 桌面之外，请用 genea CLI 或 "
             "genealogy-server。";
    }
    return false;
  }

  // OpenGL 3.2+ / GLSL 130（设计文档 3.4 口径，Win/Linux 通用）
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

  GLFWwindow* window = glfwCreateWindow(options_.width, options_.height,
                                        "家谱 · genealogy", nullptr, nullptr);
  if (window == nullptr) {
    if (err != nullptr) *err = "创建窗口失败（OpenGL 3.2 上下文不可用）";
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);   // 开垂直同步：家谱数据不是高频刷新场景，
                         // 省电、减少无谓 GPU 占用比帧数高更重要

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // 全键盘可达（6.4）
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 设计文档 3.4 docking
  io.IniFilename = nullptr;   // 不写 imgui.ini：布局随版本漂移，
                              // 每台机器自己存一份不如用代码里的默认布局

  apply_theme(options_.high_contrast);

  // 中文字体（失败不致命：回退默认字体 + 状态栏提示）
  std::string font_err;
  const bool font_failed =
      !load_chinese_font(io, options_.font_size, &font_err);
  if (font_failed) {
    state_.set_status(font_err, false);
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  build_panels();

  // 启动时带 --db 就直接开（桌面双击 / 命令行两种入口同一条路径）。
  // 字体提示并入开库状态，不互相覆盖（第三轮新-C13：字体失败提示
  // 曾被随后的「已打开 …」直接冲掉，方框字症状查不到原因）
  if (!options_.db_path.empty()) {
    std::string why;
    if (!session_.open(options_.db_path, &why)) {
      state_.set_status("打开数据库失败: " + why, true);
    } else {
      // 第六轮 低-7：世系表以 state_.db_path 作缓存失效键，启动时须同步，
      // 否则首帧判定基准是空串，与实际会话路径脱节。
      state_.db_path = options_.db_path;
      state_.set_status("已打开 " + options_.db_path);
    }
    if (font_failed) {
      state_.set_status(state_.status_message + "（另: " + font_err + "）",
                        state_.status_is_error);
    }
  }

  while (glfwWindowShouldClose(window) == GLFW_FALSE && !want_quit_) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 全屏 dockspace：面板默认散布其上，可拖拽重组
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(0, vp, ImGuiDockNodeFlags_PassthruCentralNode);

    draw_menu_bar();
    // 关系快捷：人物卡按钮设置待建关系 → 自动打开新建面板
    if (!state_.new_relation_kind.empty() && panels_.size() > 1)
      panels_[1]->open = true;
    for (auto& p : panels_) {
      if (!p->open) continue;
      if (ImGui::Begin(p->title(), &p->open)) {
        p->draw(session_);
      }
      ImGui::End();
    }
    draw_status_bar();

    // 「打开数据库」弹窗。
    // OpenPopup 必须与 BeginPopupModal 在同一个窗口层级（这里都是
    // dockspace 顶层）——早先在菜单栏里 OpenPopup，弹窗根本不出现，
    // 因为菜单栏是另一个窗口，popup 层级互不相通。
    if (show_open_dialog_) {
      ImGui::OpenPopup("打开数据库");
      show_open_dialog_ = false;
    }
    if (ImGui::BeginPopupModal("打开数据库", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::SetNextItemWidth(420);
      ImGui::InputText("路径", path_buf_, sizeof(path_buf_));
      if (ImGui::Button("打开")) {
        std::string why;
        // open() 会先 close() 旧库（新库打开失败旧库也已不在）：
        // 换库前后各面板缓存与全局选中态一律清掉（复核 2.13 +
        // 第三轮新-C5），清理序列与「关闭数据库」同一份
        //（reset_for_database_switch，可无头单测）。
        reset_panels_for_switch();
        if (session_.open(path_buf_, &why)) {
          state_.db_path = path_buf_;
          state_.set_status(std::string("已打开 ") + path_buf_);
          ImGui::CloseCurrentPopup();
        } else {
          state_.set_status("打开失败: " + why, true);
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("取消")) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    ImGui::Render();
    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.06F, 0.06F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return true;
}

}  // namespace genealogy::gui
