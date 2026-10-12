#pragma once

#include <string>
#include <string_view>

namespace genealogy {

// ULID（Universally Unique Lexicographically Sortable Identifier）：
// 26 字符 Crockford Base32 编码 = 48bit 毫秒时间戳 + 80bit 随机数。
// 选型理由：字典序可排序（游标分页天然有序）、URL 安全、无连字符。
std::string generate_ulid();

// 校验 26 字符 Crockford Base32（字母表排除 I/L/O/U）。
bool is_valid_ulid(std::string_view s) noexcept;

}  // namespace genealogy
