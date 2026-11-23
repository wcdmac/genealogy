#include <genealogy/gui/font.hpp>

#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace genealogy::gui {

namespace {

#if defined(_WIN32)
std::string wide_to_utf8(const wchar_t* w) {
  if (w == nullptr) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), size, nullptr, nullptr);
  return out;
}
#endif

// P0：随包完整字体优先（避免缺字与版权风险），系统字体仅作兜底。
// 探测顺序：① exe 同目录 fonts/（CPack bin/fonts/）
//           ② exe 上级 share/genealogy/fonts/（Unix 安装布局）
//           ③ exe 上两级的 assets/fonts/（开发期 build/bin → 仓库根）
//           ④ 当前工作目录 assets/fonts/（从仓库根启动）
std::vector<std::string> bundled_font_candidates() {
  std::vector<std::string> dirs;
  std::string exe_dir;
#if defined(_WIN32)
  wchar_t exe[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (n > 0 && n < MAX_PATH) exe_dir = wide_to_utf8(exe);
#elif defined(__APPLE__)
  char buf[4096] = {};
  std::uint32_t n = sizeof(buf);
  if (_NSGetExecutablePath(buf, &n) == 0) exe_dir = std::string(buf);
#else
  char buf[4096] = {};
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) { buf[n] = '\0'; exe_dir = std::string(buf); }
#endif
  if (!exe_dir.empty()) {
    const auto slash = exe_dir.find_last_of("\\/");
    if (slash != std::string::npos) {
      const std::string dir = exe_dir.substr(0, slash);
#if defined(_WIN32)
      dirs.push_back(dir + "\\fonts\\");
      dirs.push_back(dir + "\\..\\share\\genealogy\\fonts\\");
      dirs.push_back(dir + "\\..\\..\\assets\\fonts\\");
#else
      dirs.push_back(dir + "/fonts/");
      dirs.push_back(dir + "/../share/genealogy/fonts/");
      dirs.push_back(dir + "/../../assets/fonts/");
#endif
    }
  }
  dirs.push_back("assets/fonts/");

  std::vector<std::string> out;
  for (const auto& d : dirs) {
    out.push_back(d + "SourceHanSansSC-Regular.otf");
    out.push_back(d + "SourceHanSansSC-Bold.otf");
  }
  return out;
}

// 按平台列出候选中文字体（随包字体都失败时的最后兜底）。
std::vector<std::string> font_candidates() {
#if defined(_WIN32)
  const char* windir = std::getenv("WINDIR");
  const std::string w = windir != nullptr ? windir : "C:\\Windows";
  return {
      w + "\\Fonts\\msyh.ttc",      // 微软雅黑（Win7+，覆盖面最广）
      w + "\\Fonts\\msyhbd.ttc",    // 雅黑粗体
      w + "\\Fonts\\simhei.ttf",    // 黑体（WinXP 起）
      w + "\\Fonts\\simsun.ttc",    // 宋体（最后兜底：窄但齐全）
      w + "\\Fonts\\deng.ttf",      // 等线
  };
#elif defined(__APPLE__)
  return {
      "/System/Library/Fonts/PingFang.ttc",
      "/System/Library/Fonts/STHeiti Light.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc",
      "/Library/Fonts/Songti.ttc",
  };
#else
  return {
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJKSC-Regular.otf",
      "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
      "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
      "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
      "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
  };
#endif
}

}  // namespace

bool load_chinese_font(ImGuiIO& io, float size_px, std::string* err) {
  // P0：随包字体用 Full ranges（21000+ 字），覆盖老谱生僻字；
  // ImGui 1.93 的图集对完整 CJK 较大，Oversample 降为 1 控内存。
  const ImWchar* full = io.Fonts->GetGlyphRangesChineseFull();
  const ImWchar* common = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

  ImFontConfig cfg;
  cfg.OversampleH = 1;   // Full 字集下过采样会让图集翻倍
  cfg.OversampleV = 1;
  cfg.PixelSnapH = true;
  // ① 随包完整字体（避免缺字；已随安装分发，路径见 bundled_font_candidates）
  bool loaded_bundled = false;
  for (const auto& path : bundled_font_candidates()) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) continue;
    std::fclose(f);
    if (io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg, full) != nullptr) {
      loaded_bundled = true;
      break;
    }
  }
  if (loaded_bundled) return true;

  // ② 系统字体兜底（Common ranges，控制图集大小）
  cfg.OversampleH = 2;
  for (const auto& path : font_candidates()) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) continue;
    std::fclose(f);
    if (io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg, common) !=
        nullptr) {
      return true;
    }
  }

  // 一个都没加载上：保留默认字体，把原因说清楚。
  // 不 return false 让调用方崩——界面变成英文+方框也比打不开强。
  if (err != nullptr) {
    *err = "未找到中文字体（已回退内置字体，中文将显示为方框）。候选路径：";
    for (const auto& p : font_candidates()) {
      *err += "\n  ";
      *err += p;
    }
  }
  io.Fonts->AddFontDefault();
  return false;
}

}  // namespace genealogy::gui
