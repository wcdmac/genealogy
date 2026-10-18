#pragma once

// 最小 UTF-8 工具：只做「按码位遍历」这一件事。
//
// 为什么不用 ICU/Boost.Text：core 的依赖纪律是「仅 MIT/BSD/zlib/公有领域」，
// 且当前唯一需要的操作就是按字遍历（辈字定位、姓名显示宽度）。
// 完整的 Unicode 规范化/大小写/排序都不做——也不该在 core 里做。
//
// 口径：非法字节序列按「每个非法字节各算一个码位」处理（不抛异常、
// 不跳过）。家谱数据来自人工录入，遇到坏字节时「能看到原文」比
// 「静默吞掉」更有利于谱师校对。

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace genealogy {

// 按码位切分 UTF-8 字符串。返回值是各字符的字节区间（可用来取子串）。
// 空串返回空 vector；非法字节各自成段。
std::vector<std::string_view> utf8_chars(std::string_view text);

// 码点数（= 字数）。家谱界面显示「名几字」用它。
std::size_t utf8_length(std::string_view text) noexcept;

// 是否 CJK 统一表意文字（基本区 + 扩展 A）。辈字判定用它——
// 派语联句里只取汉字，标点数字一概忽略。
bool is_cjk_ideograph(char32_t cp) noexcept;

}  // namespace genealogy
