#include <genealogy/gui/font.hpp>

#include <imgui.h>

#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace genealogy::gui {

namespace {

// 按平台列出候选中文字体（存在第一个就用）。
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
  // 1.93 的函数名：Common 版 = 默认 + 半宽 + 日文假名 + 2500 常用简体字。
  // 2500 字对家谱界面够用（Full 版 21000 字，图集会大 3 倍以上，
  // 生僻字在行传原文里仍可能出现——真遇到再切 Full）。
  const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

  // 先试系统中文字体；都失败就退回内置默认字体（英文 UI 仍可用）。
  for (const auto& path : font_candidates()) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) continue;
    std::fclose(f);
    ImFontConfig cfg;
    cfg.OversampleH = 2;   // 横向过采样：中文笔画密，默认 1 会发虚
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    if (io.Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg, ranges) !=
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
