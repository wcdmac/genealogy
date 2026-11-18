#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace genealogy {

// ULID（Universally Unique Lexicographically Sortable Identifier）：
// 26 字符 Crockford Base32 编码 = 48bit 毫秒时间戳 + 80bit 随机数。
// 选型理由：字典序可排序（游标分页天然有序）、URL 安全、无连字符。
std::string generate_ulid();

// 校验 26 字符 Crockford Base32（字母表排除 I/L/O/U）。
bool is_valid_ulid(std::string_view s) noexcept;

// 单调 ULID 生成器：进程内严格递增。generate_ulid 在同一毫秒内的两次
// 生成靠随机尾区分，大小随机——凡依赖「id 序即写入序」的表（audit_log
// 的「新变更在前」）需要这个；实体 id 不用它（严格递增会暴露写速率，
// 且引入全局锁）。
class MonotonicUlid {
 public:
  std::string next();

 private:
  std::mutex mu_;
  std::string last_;
};

}  // namespace genealogy
