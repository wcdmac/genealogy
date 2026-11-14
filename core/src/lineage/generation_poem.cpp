#include <genealogy/lineage/generation_poem.hpp>

#include <genealogy/util/utf8.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace genealogy::lineage {

std::vector<std::string> parse_poem(std::string_view text) {
  std::vector<std::string> out;
  for (const auto& ch : utf8_chars(text)) {
    if (ch.size() == 1) continue;   // ASCII 一概不当辈字
    // 解出码位判 CJK。序列不足 3 字节的不可能是表意文字。
    if (ch.size() < 3) continue;
    const auto* b = reinterpret_cast<const unsigned char*>(ch.data());
    const char32_t cp = static_cast<char32_t>(
        (static_cast<std::uint32_t>(b[0] & 0x0F) << 12) |
        (static_cast<std::uint32_t>(b[1] & 0x3F) << 6) |
        static_cast<std::uint32_t>(b[2] & 0x3F));
    if (is_cjk_ideograph(cp)) out.emplace_back(ch);
  }
  return out;
}

std::vector<NameHit> locate_in_name(const std::vector<std::string>& poem,
                                    const std::string& name) {
  std::vector<NameHit> out;
  if (poem.empty() || name.empty()) return out;

  // 名字按字切；辈字在派语里的位置查表（同字多次出现全部记录）。
  const auto chars = utf8_chars(name);
  for (std::size_t pos = 0; pos < chars.size(); ++pos) {
    const std::string c(chars[pos]);
    for (int seq = 1; seq <= static_cast<int>(poem.size()); ++seq) {
      if (poem[static_cast<std::size_t>(seq - 1)] == c) {
        out.push_back(NameHit{pos, seq});
      }
    }
  }
  return out;
}

std::optional<int> expected_seq(const std::vector<std::string>& poem,
                                const std::string& given_name) {
  const auto hits = locate_in_name(poem, given_name);
  if (hits.empty()) return std::nullopt;
  // 惯例：辈字取名字的第一个字。双名首字命中优先；首字没命中才
  // 退到「本字命中」（单名）。位置相同时取辈序小者（更常见）。
  const auto best = *std::min_element(hits.begin(), hits.end(),
                                      [](const NameHit& a, const NameHit& b) {
                                        if (a.position != b.position) {
                                          return a.position < b.position;
                                        }
                                        return a.seq < b.seq;
                                      });
  return best.seq;
}

std::vector<PoemConflict> check_poem_consistency(
    const PersonGraph& g,
    const std::unordered_map<std::string, int>& generations,
    const std::vector<std::string>& poem, int poem_start,
    const std::unordered_set<std::string>* member_filter) {
  std::vector<PoemConflict> out;
  if (poem.empty()) return out;

  for (const auto& [id, p] : g.nodes()) {
    if (member_filter != nullptr && member_filter->count(id) == 0) continue;
    const auto gen_it = generations.find(id);
    if (gen_it == generations.end()) continue;   // 世代断链：不判
    const int generation_no = gen_it->second;

    const auto name_seq = expected_seq(poem, p->given_name);
    if (!name_seq) continue;   // 名字里没有辈字：不判（外亲/单名俗）

    const int expected = poem_start + *name_seq - 1;
    if (expected == generation_no) continue;   // 一致：静默

    PoemConflict c;
    c.person_id = id;
    c.person_name = p->family_name + p->given_name;
    c.name_seq = name_seq;
    c.generation_no = generation_no;
    c.expected_no = expected;
    c.detail = "名字辈字在第 " + std::to_string(*name_seq) + " 句（期望第 " +
               std::to_string(expected) + " 世），实际自始祖起算第 " +
               std::to_string(generation_no) + " 世";
    out.push_back(std::move(c));
  }
  return out;
}

}  // namespace genealogy::lineage
