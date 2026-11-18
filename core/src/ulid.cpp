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

}  // namespace

std::string generate_ulid() {
  char buf[26];
  encode(static_cast<std::uint64_t>(now_ms()), 10, buf);
  encode(next_random(), 16, buf + 10);
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
