#include <genealogy/server/auth.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>

namespace genealogy::server {
namespace auth {

namespace {

// ---------------------------------------------------------------- SHA-256
// 标准实现（FIPS 180-4）。自带的理由见头文件注释。

struct Sha256 {
  std::uint32_t h[8];
  std::uint64_t len = 0;
  unsigned char buf[64];
  std::size_t buflen = 0;

  Sha256() { reset(); }

  void reset() {
    static const std::uint32_t kInit[8] = {0x6a09e667u, 0xbb67ae85u,
                                           0x3c6ef372u, 0xa54ff53au,
                                           0x510e527fu, 0x9b05688cu,
                                           0x1f83d9abu, 0x5be0cd19u};
    std::memcpy(h, kInit, sizeof(h));
    len = 0;
    buflen = 0;
  }

  static std::uint32_t rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }

  void block(const unsigned char* p) {
    static const std::uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
        0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
        0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
             (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = S0 + maj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }

  void update(const void* data, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    len += n;
    while (n > 0) {
      const std::size_t take = std::min(n, 64 - buflen);
      std::memcpy(buf + buflen, p, take);
      buflen += take;
      p += take;
      n -= take;
      if (buflen == 64) {
        block(buf);
        buflen = 0;
      }
    }
  }

  std::array<unsigned char, 32> digest() {
    std::array<unsigned char, 32> out{};
    const std::uint64_t bits = len * 8;
    unsigned char pad = 0x80;
    update(&pad, 1);
    pad = 0x00;
    while (buflen != 56) update(&pad, 1);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; ++i) {
      lenbuf[i] = static_cast<unsigned char>(bits >> (56 - i * 8));
    }
    update(lenbuf, 8);
    for (int i = 0; i < 8; ++i) {
      out[i * 4] = static_cast<unsigned char>(h[i] >> 24);
      out[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16);
      out[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8);
      out[i * 4 + 3] = static_cast<unsigned char>(h[i]);
    }
    return out;
  }
};

std::array<unsigned char, 32> sha256(const std::string& s) {
  Sha256 h;
  h.update(s.data(), s.size());
  return h.digest();
}

// HMAC-SHA256（RFC 2104）
std::array<unsigned char, 32> hmac_sha256(const std::string& key,
                                          const std::string& msg) {
  std::array<unsigned char, 64> k{};
  if (key.size() > 64) {
    const auto d = sha256(key);
    std::memcpy(k.data(), d.data(), d.size());
  } else {
    std::memcpy(k.data(), key.data(), key.size());
  }
  unsigned char ipad[64];
  unsigned char opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = static_cast<unsigned char>(k[i] ^ 0x36);
    opad[i] = static_cast<unsigned char>(k[i] ^ 0x5c);
  }
  Sha256 inner;
  inner.update(ipad, 64);
  inner.update(msg.data(), msg.size());
  const auto id = inner.digest();
  Sha256 outer;
  outer.update(opad, 64);
  outer.update(id.data(), id.size());
  return outer.digest();
}

// ---------------------------------------------------------------- base64url

const char* kB64Url =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const std::string& in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  int val = 0;
  int bits = 0;
  for (const unsigned char c : in) {
    val = (val << 8) | c;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out.push_back(kB64Url[(val >> bits) & 0x3F]);
    }
  }
  if (bits > 0) {
    out.push_back(kB64Url[(val << (6 - bits)) & 0x3F]);
  }
  return out;   // JWT 规定不带 padding
}

int b64url_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

bool base64url_decode(const std::string& in, std::string* out) {
  out->clear();
  int val = 0;
  int bits = 0;
  for (const char c : in) {
    const int v = b64url_value(c);
    if (v < 0) return false;
    val = (val << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out->push_back(static_cast<char>((val >> bits) & 0xFF));
    }
  }
  return true;
}

// ---------------------------------------------------------------- 密钥

// 编译期内置默认值：只够本地开发。公网部署必须设
// GENEALOGY_JWT_SECRET，否则任何人都能伪造 steward 令牌。
constexpr const char* kDefaultSecret = "genealogy-dev-only-change-me";

std::string secret() {
  const char* env = std::getenv("GENEALOGY_JWT_SECRET");
  if (env != nullptr && *env != '\0') return env;
  static bool warned = false;
  if (!warned) {
    std::fprintf(stderr,
                 "警告: 未设 GENEALOGY_JWT_SECRET，使用内置默认密钥"
                 "（仅限本地开发，公网部署必须显式设置）\n");
    warned = true;
  }
  return kDefaultSecret;
}

std::int64_t now_seconds() {
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

std::string json_escape(const std::string& s) {
  std::string out;
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// 极简 JSON 取字符串/整数字段（JWT 的 payload 结构固定，不值得引解析器）
std::optional<std::string> json_string_field(const std::string& json,
                                             const std::string& key) {
  const std::string pat = "\"" + key + "\":\"";
  std::size_t p = json.find(pat);
  if (p == std::string::npos) return std::nullopt;
  p += pat.size();
  std::string out;
  while (p < json.size() && json[p] != '"') {
    if (json[p] == '\\' && p + 1 < json.size()) {
      const char n = json[p + 1];
      p += 2;
      switch (n) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        default: out += n;
      }
      continue;
    }
    out += json[p++];
  }
  return out;
}

std::optional<std::int64_t> json_int_field(const std::string& json,
                                           const std::string& key) {
  const std::string pat = "\"" + key + "\":";
  std::size_t p = json.find(pat);
  if (p == std::string::npos) return std::nullopt;
  p += pat.size();
  while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) {
    ++p;
  }
  std::string num;
  while (p < json.size() &&
         (std::isdigit(static_cast<unsigned char>(json[p])) || json[p] == '-')) {
    num += json[p++];
  }
  if (num.empty()) return std::nullopt;
  try {
    return static_cast<std::int64_t>(std::stoll(num));
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

const char* to_string(Role r) {
  switch (r) {
    case Role::Steward: return "steward";
    case Role::Editor: return "editor";
    case Role::Viewer: return "viewer";
  }
  return "viewer";
}

std::optional<Role> role_from_string(const std::string& s) {
  if (s == "steward") return Role::Steward;
  if (s == "editor") return Role::Editor;
  if (s == "viewer") return Role::Viewer;
  return std::nullopt;
}

std::string issue_token(const std::string& username, Role role,
                        std::int64_t ttl_seconds) {
  const std::string header =
      base64url_encode(R"({"alg":"HS256","typ":"JWT"})");
  std::ostringstream payload;
  payload << "{\"sub\":\"" << json_escape(username) << "\",\"role\":\""
          << to_string(role) << "\",\"iat\":" << now_seconds()
          << ",\"exp\":" << now_seconds() + ttl_seconds << "}";
  const std::string body = base64url_encode(payload.str());

  const std::string signing_input = header + "." + body;
  const auto mac = hmac_sha256(secret(), signing_input);
  // data() 是 unsigned char*，std::string 的指针构造只收 const char*，
  // 直接传会掉进 initializer_list 重载然后报一串看不懂的错。
  const std::string sig(reinterpret_cast<const char*>(mac.data()), mac.size());
  return signing_input + "." + base64url_encode(sig);
}

bool verify_token(const std::string& token, Claims* out,
                  std::string* error) {
  const std::size_t d1 = token.find('.');
  if (d1 == std::string::npos) {
    *error = "令牌结构损坏（缺第一段分隔符）";
    return false;
  }
  const std::size_t d2 = token.find('.', d1 + 1);
  if (d2 == std::string::npos) {
    *error = "令牌结构损坏（缺第二段分隔符）";
    return false;
  }
  const std::string header_b64 = token.substr(0, d1);
  const std::string body_b64 = token.substr(d1 + 1, d2 - d1 - 1);
  const std::string sig_b64 = token.substr(d2 + 1);

  // 先验签，再解析内容——反过来的话未验签的 payload 已经被信了
  const std::string signing_input = header_b64 + "." + body_b64;
  const auto want = hmac_sha256(secret(), signing_input);
  const std::string want_b64 = base64url_encode(
      std::string(reinterpret_cast<const char*>(want.data()), want.size()));
  // 常量时间比较：逐位 OR 累积差异，不提前退出。
  // 用 != 比较会在第一个不同字节处返回，攻击者可据此按字节爆破签名
  // （经典的时序侧信道）。本地部署风险有限，但写法不该分场景。
  if (sig_b64.size() != want_b64.size()) {
    *error = "签名不符";
    return false;
  }
  unsigned char diff = 0;
  for (std::size_t i = 0; i < sig_b64.size(); ++i) {
    diff |= static_cast<unsigned char>(sig_b64[i] ^ want_b64[i]);
  }
  if (diff != 0) {
    *error = "签名不符";
    return false;
  }

  std::string payload;
  if (!base64url_decode(body_b64, &payload)) {
    *error = "payload 不是合法 base64url";
    return false;
  }
  auto sub = json_string_field(payload, "sub");
  auto role = json_string_field(payload, "role");
  auto exp = json_int_field(payload, "exp");
  if (!sub || !role || !exp) {
    *error = "payload 缺 sub/role/exp";
    return false;
  }
  if (*exp < now_seconds()) {
    *error = "令牌已过期";
    return false;
  }
  auto r = role_from_string(*role);
  if (!r) {
    *error = "未知角色: " + *role;
    return false;
  }
  out->username = *sub;
  out->role = *r;
  out->expires_at = *exp;
  return true;
}

Claims local_identity() {
  Claims c;
  c.username = "local";
  c.role = Role::Steward;
  c.expires_at = 0;   // 永不过期
  return c;
}

}  // namespace auth
}  // namespace genealogy::server
