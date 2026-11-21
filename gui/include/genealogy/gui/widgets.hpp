#pragma once

// ImGui 小部件桥接层。
//
// 为什么需要：vendored 的 ImGui 1.93.1 docking 快照只有 char* 版
// InputText（std::string 重载在更新版本里才有）。这里用官方 demo 的
// CallbackResize 桥接模式：ImGui 需要更大缓冲时回调里同步扩容
// std::string，缓冲与 string 同生共死——不再有固定 cap 截断（第三轮
// 新-C14：旧实现把内容拷进固定栈缓冲，超长文本在编辑框里直接丢尾，
// 一保存就永久丢数据）。

#include <imgui.h>

#include <string>

namespace genealogy::gui {

namespace detail {

// InputText/InputTextMultiline 共用的缓冲扩容回调：BufSize 含 NUL
// 终结位，resize 到它并把 Buf 指回 string 存储。
inline int resize_callback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* s = static_cast<std::string*>(data->UserData);
    s->resize(static_cast<std::size_t>(data->BufSize));
    data->Buf = s->data();
  }
  return 0;
}

}  // namespace detail

// 短文本输入。返回 true 表示本次编辑改变了值。
// cap 参数保留仅为调用点兼容：CallbackResize 下不再截断，语义已废。
inline bool input_text(const char* label, std::string* value,
                       std::size_t cap = 256) {
  (void)cap;
  // ImGui 要求缓冲可写且容纳现有内容 + NUL；保证容量冗余，扩容走回调
  if (value->capacity() <= value->size()) value->reserve(value->size() + 64);
  const bool changed = ImGui::InputText(
      label, value->data(), static_cast<int>(value->capacity() + 1),
      ImGuiInputTextFlags_CallbackResize, detail::resize_callback,
      static_cast<void*>(value));
  // resize 回调可能把 string 扩到含填充位；收回到真实文本长度。
  // 先拷出再赋回（不能 operator= 自身缓冲——重分配后源指针悬空）
  std::string updated(value->data());
  *value = std::move(updated);
  return changed;
}

// 多行文本输入（行传/备注等长文本）。返回 true 表示本次编辑改变了值。
inline bool input_text_multiline(const char* label, std::string* value,
                                 float height) {
  if (value->capacity() <= value->size()) value->reserve(value->size() + 256);
  const bool changed = ImGui::InputTextMultiline(
      label, value->data(), static_cast<int>(value->capacity() + 1),
      ImVec2(-1, height), ImGuiInputTextFlags_CallbackResize,
      detail::resize_callback, static_cast<void*>(value));
  std::string updated(value->data());
  *value = std::move(updated);
  return changed;
}

}  // namespace genealogy::gui
