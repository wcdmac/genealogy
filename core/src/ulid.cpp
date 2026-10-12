#include <genealogy/ulid.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <thread>

namespace genealogy {
namespace {

constexpr char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// 每线程独立 PRNG：无全局锁，random_device 播种一次。
std::uint64_t next_random() {
  static thread_local std::mt19937_64 rng(
      std::random_device{}() ^
      static_cast<std::uint64_t>(
          std::hash<std::thread::id>{}(std::this_thread::get_id())));
  return rng();
}

std::int64_t now_ms() {
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

// 低位优先写入 chars 个字符（ULID 规范：时间戳高位在左）。
void encode(std::uint64_t value, int chars, char* out) {
  for (int i = chars - 1; i >= 0; --i) {
    out[i] = kCrockford[value & 0x1F];
    value >>= 5;
  }
}

}  // namespace

std::string generate_ulid() {
  char buf[26];
  encode(static_cast<std::uint64_t>(now_ms()), 10, buf);
  encode(next_random(), 16, buf + 10);
  return std::string(buf, 26);
}

bool is_valid_ulid(std::string_view s) noexcept {
  if (s.size() != 26) return false;
  for (const char c : s) {
    const bool is_digit = c >= '0' && c <= '9';
    const bool is_alpha = c >= 'A' && c <= 'Z';
    if (!is_digit && !is_alpha) return false;
    if (c == 'I' || c == 'L' || c == 'O' || c == 'U') return false;
  }
  return true;
}

}  // namespace genealogy
