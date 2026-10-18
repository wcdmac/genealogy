#include <genealogy/util/utf8.hpp>

#include <cstdint>

namespace genealogy {

namespace {

// 取首个字节声明的序列长度。非法首字节返回 1（按单字节段处理）。
std::size_t seq_len(unsigned char lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;   //  continuation byte 或非法首字节：按 1 字节段
}

}  // namespace

std::vector<std::string_view> utf8_chars(std::string_view text) {
  std::vector<std::string_view> out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const std::size_t n = seq_len(static_cast<unsigned char>(text[i]));
    // 序列超出剩余长度时截断到尾部（同样不抛异常）
    const std::size_t take = (i + n <= text.size()) ? n : text.size() - i;
    out.emplace_back(text.data() + i, take);
    i += take;
  }
  return out;
}

std::size_t utf8_length(std::string_view text) noexcept {
  return utf8_chars(text).size();
}

bool is_cjk_ideograph(char32_t cp) noexcept {
  // 基本区 U+4E00–U+9FFF + 扩展 A U+3400–U+4DBF + 兼容表意文字
  // U+F900–U+FAFF（繁体谱常见）。扩展 B 及以后在 BMP 外，先不管——
  // 派语用字都在常用区，真遇到生僻字按非辈字处理（宁可漏，不可错配）。
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0xF900 && cp <= 0xFAFF);
}

}  // namespace genealogy
