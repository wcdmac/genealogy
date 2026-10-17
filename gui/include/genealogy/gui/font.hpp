#pragma once

// 中文字体加载：ImGui 的默认字体不含汉字，不加载中文字体的话所有
// 中文界面文字会变成一片方框（tofu）——家谱软件的界面几乎全是中文，
// 这是「看得见」与「不能用」的区别。
//
// 取舍：用 ImGui 内置的 stb_truetype 而不是设计文档提的 FreeType。
// 理由：stb 已随 imgui 编译进来（零新增依赖、零链接步骤），中文渲染
// 质量对 UI 够用；FreeType 要多链一个库、多一份许可证条目，收益
// 主要是微调 hinting，一期不值当。哪天要出印刷级文字再换。
//
// 字体来源：不把几 MB 的 TTF 塞进 git（那是设计文档「自托管字体」的
// 理想形态，但仓库体积与许可证清点都变麻烦）。改为按平台探测系统中文字体：
//   Windows  微软雅黑 / 黑体 / 等线
//   Linux    Noto Sans CJK / WenQuanYi / Droid Sans Fallback
//   macOS    PingFang
// 都找不到时返回 false + err 说明，调用方回退默认字体并在状态栏提示——
// 不能崩：CI 的无 GUI 环境与极简系统上，程序仍应能起来。

#include <string>

struct ImGuiIO;

namespace genealogy::gui {

// 把中文字体挂到 io.Fonts 并设为默认。成功返回 true。
// size_px 建议取 15..18；高 DPI 屏由 ImGui 的 style.ScaleAllSizes 处理。
bool load_chinese_font(ImGuiIO& io, float size_px, std::string* err);

}  // namespace genealogy::gui
