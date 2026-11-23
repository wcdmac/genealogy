#include <genealogy/ulid.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
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

// 随机段需要 80 bit（16 个 Crockford 字符 × 5 bit）。单个 mt19937_64
// 只有 64 bit，直接用会让高 16 bit 恒 0（前 3 个字符恒 '000'）。
// 这里用两个独立样本拼成 128 bit，只取低 80 bit 填入。
void encode80(char* out) {
  const std::uint64_t hi = next_random();
  const std::uint64_t lo = next_random();
  // 取 128 bit 样本的低 80 bit，每 5 bit 一个 Crockford 字符。
  for (int i = 0; i < 16; ++i) {
    const int bit = i * 5;
    // bit=0 时 hi<<64 是 UB，故只在跨 64 位边界（bit=60）时借位。
    std::uint64_t chunk;
    if (bit + 5 <= 64) {
      chunk = (lo >> bit) & 0x1F;
    } else if (bit < 64) {
      chunk = ((lo >> bit) | (hi << (64 - bit))) & 0x1F;
    } else {
      chunk = (hi >> (bit - 64)) & 0x1F;
    }
    out[15 - i] = kCrockford[chunk];
  }
}

// 负时间戳（系统时钟早于 1970）回绕成巨大值，clamp 到 0。
std::uint64_t safe_now_ms() {
  const std::int64_t ms = now_ms();
  return ms < 0 ? 0 : static_cast<std::uint64_t>(ms);
}

}  // namespace

std::string generate_ulid() {
  char buf[26];
  encode(safe_now_ms(), 10, buf);
  encode80(buf + 10);
  return std::string(buf, 26);
}

// 128 位值 +1（Crockford 字母表升序 = 字典序，从右向左进位）。
// 随机段全满才会进位到时间戳段（2^-80 量级，实际不可达）。
std::string bump_ulid(const std::string& id) {
  std::string out = id;
  for (int i = 25; i >= 0; --i) {
    const char* pos = std::strchr(kCrockford, out[i]);
    const int v = pos != nullptr ? static_cast<int>(pos - kCrockford) : 0;
    if (v + 1 < 32) {
      out[i] = kCrockford[v + 1];
      return out;
    }
    out[i] = kCrockford[0];
  }
  return out;
}

bool is_valid_ulid(std::string_view s) noexcept {
  if (s.size() != 26) return false;
  for (const char c : s) {
    const bool is_digit = c >= '0' && c <= '9';
    const bool is_alpha = c >= 'A' && c <= 'Z';
    if (!is_digit && !is_alpha) return false;
    if (c == 'I' || c == 'L' || c == 'O' || c == 'U') return false;
  }
  // 时间戳 48 bit 编入 10 个 Crockford 字符（50 bit），首位只能是 0..7；
  // "8..." 起的串解码后超出 48 bit 时间域，不是本实现能产生的 ULID。
  const char first = s[0];
  const int v = first >= '0' && first <= '9'
      ? first - '0'
      : 10 + (first >= 'A' ? first - 'A' : first - 'a');
  if (v > 7) return false;
  return true;
}

std::string MonotonicUlid::next() {
  std::lock_guard<std::mutex> lk(mu_);
  std::string id = generate_ulid();
  if (!last_.empty() && id <= last_) id = bump_ulid(last_);
  last_ = id;
  return id;
}

}  // namespace genealogy
