#pragma once

// ImGui 小部件桥接层。
//
// 为什么需要：vendored 的 ImGui 1.93.1 docking 快照只有 char* 版
// InputText（std::string 重载在更新版本里才有）。而面板状态用 std::string
// 更顺手（直接喂给 core 的 setter）。这里补一个按需同步的桥接：
// 每帧把 string 拷进栈缓冲交给 ImGui，用户改了再拷回去。
// 短字段（姓名/日期/id）栈缓冲足够；长文本（行传）用专门的大缓冲成员，
// 不在每帧做 KB 级拷贝。

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace genealogy::gui {

// 短文本输入。返回 true 表示本次编辑改变了值。
inline bool input_text(const char* label, std::string* value,
                       std::size_t cap = 256) {
  char buf[256];
  const std::size_t n = value->size() < cap ? value->size() : cap;
  std::memcpy(buf, value->data(), n);
  buf[n] = '\0';
  if (ImGui::InputText(label, buf, sizeof(buf))) {
    *value = buf;
    return true;
  }
  // 用户没编辑时也可能因截断而内容变短（外部改了 string）——
  // 不写回，保持缓冲为真，避免输入过程中的抖动。
  return false;
}

// 多行文本输入（行传等长文本）。缓冲区由调用方持有，避免每帧 KB 级拷贝。
// 返回 true 表示本次编辑改变了值。
inline bool input_text_multiline(const char* label, char* buf,
                                 std::size_t buf_size, float height) {
  return ImGui::InputTextMultiline(label, buf, buf_size,
                                   ImVec2(-1, height));
}

}  // namespace genealogy::gui
