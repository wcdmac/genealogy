// GEDCOM 5.5.1 读写实现。分层见 gedcom.hpp 头部说明。

#include <genealogy/io/gedcom.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <utility>

#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

namespace genealogy::io {

namespace {

// ---------------------------------------------------------------- 工具

std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

std::string upper_ascii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool starts_with(std::string_view s, std::string_view p) noexcept {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// UTF-8 字符边界回退：从 byte 位置 pos 往前找到不超过 limit 的合法起点
std::size_t utf8_back(const std::string& s, std::size_t pos, std::size_t limit) {
  while (pos > limit && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
  return pos;
}

std::size_t utf8_len(unsigned char lead) noexcept {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;   // 非法字节按 1 字节算，切坏了顶多显示成问号
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// ---------------------------------------------------------------- 字符集

// GEDCOM 5.5.1 允许的 CHAR 值。ANSEL / MacRoman 明确不支持：
// 前者需要约 250 条组合字符映射表，后者几乎只出现在 90 年代 Mac 家谱软件，
// 都没有权威离线依据，猜字节等于毁数据。
enum class Encoding { Utf8, Utf16LE, Utf16BE, Cp1252, Latin1 };

std::string encoding_name(Encoding e) {
  switch (e) {
    case Encoding::Utf8: return "UTF-8";
    case Encoding::Utf16LE: return "UTF-16LE";
    case Encoding::Utf16BE: return "UTF-16BE";
    case Encoding::Cp1252: return "CP1252";
    case Encoding::Latin1: return "LATIN1";
  }
  return "UNKNOWN";
}

bool bom_of(std::string_view bytes, Encoding* enc, std::size_t* bom_len) {
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
  if (bytes.size() >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
    *enc = Encoding::Utf8;
    *bom_len = 3;
    return true;
  }
  if (bytes.size() >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
    *enc = Encoding::Utf16LE;
    *bom_len = 2;
    return true;
  }
  if (bytes.size() >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
    *enc = Encoding::Utf16BE;
    *bom_len = 2;
    return true;
  }
  return false;
}

// 在前几行里找 "1 CHAR xxx" 并归一化为受支持的字符集。
// GEDCOM 规定 HEAD 必须在最前面，因此只看前 8KB 足够。
// 不认识的值一律拒收：解错字符集比拒收危险得多。
bool encoding_from_char_tag(std::string_view bytes, Encoding* out,
                            std::string* declared) {
  const std::size_t window = std::min<std::size_t>(bytes.size(), 8192);
  std::size_t pos = 0;
  while (pos < window) {
    std::size_t eol = bytes.find('\n', pos);
    if (eol == std::string_view::npos) eol = bytes.size();
    std::string_view line = bytes.substr(pos, eol - pos);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (starts_with(line, "1 CHAR ") || starts_with(line, "1 char ")) {
      const std::string v = upper_ascii(trim(line.substr(7)));
      *declared = v;
      if (v == "UTF-8" || v == "UTF8") { *out = Encoding::Utf8; return true; }
      if (v == "UNICODE") { *out = Encoding::Utf16LE; return true; }  // 规范定义
      if (v == "LATIN1" || v == "ISO-8859-1") { *out = Encoding::Latin1; return true; }
      if (v == "CP1252" || v == "WINDOWS-1252" || v == "MSWINDOWS") {
        *out = Encoding::Cp1252;
        return true;
      }
      return false;
    }
    pos = eol + 1;
  }
  return false;
}

std::string decode_bytes(std::string_view bytes, Encoding enc) {
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
  const std::size_t n = bytes.size();
  std::string out;
  out.reserve(n);
  switch (enc) {
    case Encoding::Utf8:
      out.assign(bytes.data(), n);
      break;
    case Encoding::Latin1:
      for (std::size_t i = 0; i < n; ++i) append_utf8(out, p[i]);
      break;
    case Encoding::Cp1252:
      // CP1252 的 0x80-0x9F 区只有 27 个码位有定义，其余按 U+FFFD
      for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t cp = p[i];
        if (cp >= 0x80 && cp <= 0x9F) {
          static const std::uint32_t kCp1252High[32] = {
              0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
              0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
              0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
              0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
          cp = kCp1252High[cp - 0x80];
        }
        append_utf8(out, cp);
      }
      break;
    case Encoding::Utf16LE:
    case Encoding::Utf16BE: {
      for (std::size_t i = 0; i + 1 < n; i += 2) {
        const std::uint32_t cp = enc == Encoding::Utf16LE
                                     ? (p[i] | (p[i + 1] << 8))
                                     : ((p[i] << 8) | p[i + 1]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < n) {
          const std::uint32_t lo = enc == Encoding::Utf16LE
                                       ? (p[i + 2] | (p[i + 3] << 8))
                                       : ((p[i + 2] << 8) | p[i + 3]);
          if (lo >= 0xDC00 && lo <= 0xDFFF) {
            append_utf8(out, 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00));
            i += 2;
            continue;
          }
        }
        append_utf8(out, cp);
      }
      break;
    }
  }
  return out;
}

// ---------------------------------------------------------------- 语法解析

struct FlatLine {
  int level = 0;
  std::string xref;
  std::string tag;
  std::string value;
  int line_no = 0;
};

// 拆一行。返回 false 表示该行不是合法 GEDCOM 行（调用方记 loss 并跳过）。
bool split_line(std::string_view line, FlatLine* out) {
  std::size_t i = 0;
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
  if (i == 0) return false;                       // 没有 level
  const std::string lvl(line.substr(0, i));
  if (lvl.size() > 3) return false;
  out->level = std::stoi(lvl);
  while (i < line.size() && line[i] == ' ') ++i;
  if (i < line.size() && line[i] == '@') {
    const std::size_t end = line.find('@', i + 1);
    if (end == std::string_view::npos) return false;
    out->xref = std::string(line.substr(i + 1, end - i - 1));
    i = end + 1;
    while (i < line.size() && line[i] == ' ') ++i;
  }
  const std::size_t tag_begin = i;
  while (i < line.size() && line[i] != ' ') ++i;
  out->tag = upper_ascii(std::string(line.substr(tag_begin, i - tag_begin)));
  if (out->tag.empty()) return false;
  // 规范：tag 与 value 之间恰好一个空格是定界符，多余空格属于 value。
  // 但 CONT 行的前导空格按规范同样只剥一个——此处统一剥一个，避免
  // 把谱师故意加的缩进吞掉。
  if (i < line.size()) {
    ++i;
    out->value = std::string(line.substr(i));
    // GEDCOM @ 转义解出（第五轮 P2-9）：值里 @@ → @，与写出侧 text()
    // 的转义对称。指针值（@S1@）不含 @@，原样通过。
    std::string unescaped;
    unescaped.reserve(out->value.size());
    for (std::size_t k = 0; k < out->value.size(); ++k) {
      unescaped += out->value[k];
      if (out->value[k] == '@' && k + 1 < out->value.size() &&
          out->value[k + 1] == '@') {
        ++k;
      }
    }
    out->value = std::move(unescaped);
  }
  return true;
}

// 嵌套深度上限（2026-11 静态审查 H6）：无界递归在畸形深嵌套文件上
// 会栈溢出压垮导入进程。GEDCOM 真实嵌套 ≤6 层，64 已是 10 倍余量。
constexpr std::size_t kMaxParseDepth = 64;
constexpr std::size_t kDepthSentinel = static_cast<std::size_t>(-1);

std::size_t build_subtree(const std::vector<FlatLine>& f, std::size_t i,
                          GedcomNode* out, std::size_t depth = 0) {
  if (depth > kMaxParseDepth) return kDepthSentinel;
  out->level = f[i].level;
  out->xref = f[i].xref;
  out->tag = f[i].tag;
  out->value = f[i].value;
  std::size_t j = i + 1;
  // 跳级（level 不连续）按「上一级的子节点」容忍处理，不让整份文件报废
  while (j < f.size() && f[j].level > f[i].level) {
    GedcomNode child;
    j = build_subtree(f, j, &child, depth + 1);
    if (j == kDepthSentinel) return kDepthSentinel;
    out->children.push_back(std::move(child));
  }
  return j;
}

// CONT/CONC 合并：CONT 值前补换行，CONC 值直接接上。
// 递归自底向上做，保证嵌套结构里的续行也不丢。
void merge_continuations(GedcomNode* node) {
  std::string acc;
  std::vector<GedcomNode> kept;
  kept.reserve(node->children.size());
  for (auto& c : node->children) {
    if (c.tag == "CONT") {
      acc += "\n";
      acc += c.value;
    } else if (c.tag == "CONC") {
      acc += c.value;
    } else {
      kept.push_back(std::move(c));
    }
  }
  node->value += acc;
  node->children = std::move(kept);
  for (auto& c : node->children) merge_continuations(&c);
}

const GedcomNode* find_record(const GedcomDocument& doc, std::string_view tag) {
  for (const auto& r : doc.records) {
    if (r.tag == tag) return &r;
  }
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- 语法层 API

const GedcomNode* GedcomNode::child(std::string_view tag) const noexcept {
  for (const auto& c : children) {
    if (c.tag == tag) return &c;
  }
  return nullptr;
}

std::vector<const GedcomNode*> GedcomNode::children_all(
    std::string_view tag) const {
  std::vector<const GedcomNode*> out;
  for (const auto& c : children) {
    if (c.tag == tag) out.push_back(&c);
  }
  return out;
}

std::string GedcomNode::child_value(std::string_view tag) const {
  const auto* c = child(tag);
  return c ? c->value : std::string{};
}

const GedcomNode* GedcomDocument::header() const noexcept {
  return find_record(*this, "HEAD");
}

Result<GedcomDocument> parse_gedcom(std::string_view bytes) {
  Encoding enc = Encoding::Utf8;
  std::size_t bom_len = 0;
  bom_of(bytes, &enc, &bom_len);
  std::string_view body = bytes.substr(bom_len);

  // HEAD.CHAR 的优先级高于 BOM（规范：CHAR 说明的是文件实际编码，
  // BOM 只是字节序标记）；不一致时以 CHAR 为准。
  Encoding char_enc = Encoding::Utf8;
  std::string declared;
  if (encoding_from_char_tag(body, &char_enc, &declared)) {
    enc = char_enc;
  } else if (!declared.empty()) {
    // 认识 BOM、但 CHAR 写了解不了的值：拒收，不猜
    return Error{ErrorCode::IoError,
                 "GEDCOM 声明字符集 " + declared +
                     " 暂不支持（仅支持 UTF-8 / UTF-16 / CP1252 / Latin-1）。"
                     "请先把文件转存为 UTF-8 再导入——宁可让主人转一次码，"
                     "也不把谱师的字猜成乱码。"};
  }

  const std::string text = decode_bytes(body, enc);

  GedcomDocument doc;
  doc.encoding = encoding_name(enc);

  // 拆行
  std::vector<FlatLine> flat;
  flat.reserve(256);
  std::size_t pos = 0;
  int line_no = 0;
  while (pos < text.size()) {
    std::size_t eol = text.find('\n', pos);
    if (eol == std::string_view::npos) eol = text.size();
    std::string_view line(text.data() + pos, eol - pos);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    pos = eol + 1;
    ++line_no;
    if (line.empty()) continue;
    FlatLine f;
    f.line_no = line_no;
    if (!split_line(line, &f)) {
      return Error{ErrorCode::IoError,
                   "GEDCOM 第 " + std::to_string(line_no) +
                       " 行不是合法的 level/xref/tag 结构: " +
                       std::string(line)};
    }
    flat.push_back(std::move(f));
  }
  if (flat.empty()) {
    return Error{ErrorCode::IoError, "GEDCOM 内容为空或无有效行"};
  }

  for (std::size_t i = 0; i < flat.size();) {
    GedcomNode rec;
    i = build_subtree(flat, i, &rec);
    if (i == kDepthSentinel) {
      return Error{ErrorCode::IoError,
                   "GEDCOM 嵌套超过 " +
                       std::to_string(kMaxParseDepth) +
                       " 层，疑似畸形或恶意构造的文件，拒绝导入"};
    }
    // 树深已被上限封顶，merge_continuations 的递归深度同样有界
    merge_continuations(&rec);
    doc.records.push_back(std::move(rec));
  }

  if (const auto* head = doc.header()) {
    // 5.5.1 允许 SOUR 直接带值（软件名），也允许带 NAME/VERS 子节点；
    // 两者都认，NAME 优先
    if (const auto* sour = head->child("SOUR")) {
      doc.source_name = sour->child_value("NAME");
      if (doc.source_name.empty()) doc.source_name = trim(sour->value);
      doc.source_version = sour->child_value("VERS");
    }
    if (const auto* gedc = head->child("GEDC")) {
      doc.gedcom_version = gedc->child_value("VERS");
      doc.form = gedc->child_value("FORM");
    }
  }
  return doc;
}


// ---------------------------------------------------------------- 日期
// GEDCOM DATE 文法（5.5.1）：
//   <DATE>            ::= <CAL> <DATE> | <DATE_PERIOD> | <DATE_RANGE> | INT <DATE> (<PHRASE>) | (<PHRASE>)
//   <DATE>            ::= <DAY>? <MONTH> <YEAR> | <YEAR>
//   <DATE_PERIOD>     ::= FROM <DATE> | TO <DATE> | FROM <DATE> TO <DATE>
//   <DATE_RANGE>      ::= BEF <DATE> | AFT <DATE> | BET <DATE> AND <DATE>
// 换算不出的（INT 短语、非法月日、超范围年份）不猜：保留原文 + 记 loss。

namespace {

const char* const kMonthAbbr[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

int month_index(const std::string& tok) {
  for (int i = 0; i < 12; ++i) {
    if (tok == kMonthAbbr[i]) return i + 1;
  }
  return 0;
}

// ISO 日期时间（utc_now_iso 的 "2020-01-02T03:04:05Z"）→ GEDCOM 文法
// 的 "<DATE_EXACT> [TIME]"（"2 JAN 2020 03:04:05"，日不补零）。
// HEAD/CHAN.DATE 此前直接写 ISO 串——那是 XML 的写法，宽容读取器
// 没事，严格读取器按 5.5.1 文法会拒。无法识别的格式原样返回。
std::string iso_to_gedcom_datetime(const std::string& iso) {
  if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return iso;
  try {
    const int y = std::stoi(iso.substr(0, 4));
    const int m = std::stoi(iso.substr(5, 2));
    const int d = std::stoi(iso.substr(8, 2));
    if (m < 1 || m > 12) return iso;
    std::string out =
        std::to_string(d) + " " + kMonthAbbr[m - 1] + " " + std::to_string(y);
    if (iso.size() >= 19 && iso[10] == 'T') {
      out += " " + iso.substr(11, 8);   // HH:MM:SS
    }
    return out;
  } catch (const std::exception&) {
    return iso;
  }
}

// 上式的逆：GEDCOM 日期时间 → ISO（补 Z 后缀），导入 CHAN.DATE 用。
// 与 iso_to_gedcom_datetime 精确互逆，自家往返 updated_at 不丢精度；
// 非 GEDCOM 格式（第三方任意写法、自家旧库残留的 ISO）原样返回。
std::string gedcom_datetime_to_iso(const std::string& v) {
  std::vector<std::string> toks;
  std::string cur;
  for (const char c : v) {
    if (c == ' ') {
      if (!cur.empty()) toks.push_back(std::move(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) toks.push_back(std::move(cur));
  // 文法：<日> <月缩写> <年> [HH:MM:SS]
  if (toks.size() < 3 || toks.size() > 4) return v;
  if (toks[0].empty() || toks[0].size() > 2) return v;
  const bool day_digits =
      std::all_of(toks[0].begin(), toks[0].end(),
                  [](char c) { return c >= '0' && c <= '9'; });
  if (!day_digits) return v;
  const int m = month_index(upper_ascii(toks[1]));
  if (m == 0) return v;
  const std::string& y = toks[2];
  if (y.size() != 4 ||
      !std::all_of(y.begin(), y.end(),
                   [](char c) { return c >= '0' && c <= '9'; })) {
    return v;
  }
  std::string iso = y + "-" + (m < 10 ? "0" : "") + std::to_string(m) + "-" +
                    (toks[0].size() == 1 ? "0" : "") + toks[0];
  if (toks.size() == 4) {
    if (toks[3].size() != 8 || toks[3][2] != ':' || toks[3][5] != ':') return v;
    iso += "T" + toks[3] + "Z";
  }
  return iso;
}

std::vector<std::string> split_ws(std::string_view s) {
  std::vector<std::string> out;
  std::string cur;
  for (const char c : s) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool all_digits(const std::string& s) {
  if (s.empty()) return false;
  for (const char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

// 解析单个 <DATE>：m/d 为 0 表示原文未给到那一级
bool parse_single_date(const std::string& s, int* y, int* m, int* d) {
  const auto tok = split_ws(s);
  if (tok.empty()) return false;
  if (tok.size() == 1) {
    const std::string& t = tok[0];
    if (t.size() == 10 && (t[4] == '-' || t[4] == '/') &&
        (t[7] == '-' || t[7] == '/') && all_digits(t.substr(0, 4)) &&
        all_digits(t.substr(5, 2)) && all_digits(t.substr(8, 2))) {
      *y = std::stoi(t.substr(0, 4));
      *m = std::stoi(t.substr(5, 2));
      *d = std::stoi(t.substr(8, 2));
      return true;
    }
    if (t.size() == 4 && all_digits(t)) {
      *y = std::stoi(t);
      *m = 0;
      *d = 0;
      return true;
    }
    return false;
  }
  if (tok.size() == 2) {
    const int mi = month_index(upper_ascii(tok[0]));
    if (mi > 0 && tok[1].size() == 4 && all_digits(tok[1])) {
      *y = std::stoi(tok[1]);
      *m = mi;
      *d = 0;
      return true;
    }
    return false;
  }
  if (tok.size() == 3) {
    const int mi = month_index(upper_ascii(tok[1]));
    if (mi > 0 && tok[0].size() <= 2 && all_digits(tok[0]) &&
        tok[2].size() == 4 && all_digits(tok[2])) {
      *d = std::stoi(tok[0]);
      *m = mi;
      *y = std::stoi(tok[2]);
      return true;
    }
  }
  return false;
}

struct DateOutcome {
  GenealogyDate date;
  std::string note;   // 非空 = 有损，调用方记 MappingLoss
};

DateOutcome parse_gedcom_date(std::string_view raw) {
  DateOutcome out;
  const std::string text = trim(raw);
  out.date.lunar = text;   // 原文照录：这一条保证任何 DATE 都不丢信息
  if (text.empty()) return out;

  std::string qualifier;
  bool non_gregorian = false;
  std::vector<std::vector<std::string>> groups;
  std::vector<std::string> cur;
  for (const auto& tok : split_ws(text)) {
    const std::string u = upper_ascii(tok);
    if (u == "AND") {
      if (!cur.empty()) {
        groups.push_back(cur);
        cur.clear();
      }
      continue;
    }
    if (u == "ABT" || u == "EST" || u == "CAL") {
      if (!cur.empty()) { groups.push_back(cur); cur.clear(); }
      if (qualifier.empty()) qualifier = "约";
      continue;
    }
    if (u == "BEF" || u == "AFT" || u == "TO") {
      if (!cur.empty()) { groups.push_back(cur); cur.clear(); }
      qualifier = (u == "BEF") ? "以前" : (u == "AFT" ? "以后" : "之间");
      continue;
    }
    if (u == "BET" || u == "FROM") {
      if (!cur.empty()) { groups.push_back(cur); cur.clear(); }
      if (qualifier.empty()) qualifier = "之间";
      continue;
    }
    if (u == "INT") {
      if (!cur.empty()) { groups.push_back(cur); cur.clear(); }
      out.date.precision = DatePrecision::Fuzzy;
      out.note = "日期是 INT 短语（无确定日期），仅保留原文";
      return out;
    }
    if (u.size() > 2 && u[0] == '@' && u[1] == '#') {
      if (!cur.empty()) { groups.push_back(cur); cur.clear(); }
      std::string cal = u.substr(2);
      if (!cal.empty() && cal.back() == '@') cal.pop_back();
      if (cal != "DGREGORIAN") non_gregorian = true;
      continue;
    }
    cur.push_back(tok);
  }
  if (!cur.empty()) groups.push_back(cur);

  int y = 0, m = 0, d = 0;
  bool ok = false;
  int found = 0;
  for (const auto& g : groups) {
    std::string joined;
    for (std::size_t i = 0; i < g.size(); ++i) {
      if (i) joined += ' ';
      joined += g[i];
    }
    int yy = 0, mm = 0, dd = 0;
    if (!parse_single_date(joined, &yy, &mm, &dd)) continue;
    ++found;
    if (!ok) {
      y = yy;
      m = mm;
      d = dd;
      ok = true;
    }
  }
  if (!ok) {
    out.date.precision = DatePrecision::Fuzzy;
    out.note = "日期无法解析为公历，已保留原文";
    return out;
  }
  if (y < 1 || y > 9999) {
    out.date.precision = DatePrecision::Fuzzy;
    out.note = "年份 " + std::to_string(y) + " 超出 1..9999，仅保留原文";
    return out;
  }
  if (m != 0 && (m < 1 || m > 12)) {
    out.date.precision = DatePrecision::Year;
    out.note = "月份非法，仅按年份记录";
    m = 0;
    d = 0;
  }
  if (d != 0 && m != 0) {
    static const int kLen[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = kLen[m - 1];
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max_day = 29;
    if (d < 1 || d > max_day) {
      out.date.precision = DatePrecision::Month;
      out.note = "日数非法，仅按年月记录";
      d = 0;
    }
  }

  char buf[16];
  if (d != 0) {
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    out.date.precision = DatePrecision::Day;
  } else if (m != 0) {
    std::snprintf(buf, sizeof(buf), "%04d-%02d", y, m);
    out.date.precision = DatePrecision::Month;
  } else {
    std::snprintf(buf, sizeof(buf), "%04d", y);
    out.date.precision = DatePrecision::Year;
  }
  out.date.gregorian = buf;
  if (!qualifier.empty()) out.date.qualifier = qualifier;
  if (auto gz = out.date.year_ganzhi()) out.date.ganzhi = *gz;
  if (non_gregorian) {
    out.note = "原文标注了非格里历（如儒略历），已按公历记录，可能有偏差";
  } else if (found > 1) {
    out.note = "区间/范围日期，公历字段取起始值，全文见原文";
  }
  return out;
}

// ---------------------------------------------------------------- 人名

struct NameParts {
  std::string given;
  std::string surname;
  std::string suffix;
  std::string nickname;
  bool structured = false;
};

// GEDCOM 5.5.1 个人名：Given /Surname/ Suffix。无斜杠时退到
// GIVN/SURN 子标签，再退到「默认姓 + 名前缀」拆分，最后整串记名。
NameParts parse_person_name(const GedcomNode& name,
                            const std::string& default_surname,
                            std::string* loss) {
  NameParts p;
  const std::string v = name.value;
  const std::size_t first = v.find('/');
  if (first != std::string::npos) {
    const std::size_t last = v.rfind('/');
    p.given = trim(v.substr(0, first));
    p.surname = last > first ? trim(v.substr(first + 1, last - first - 1))
                             : std::string{};
    if (last + 1 < v.size()) p.suffix = trim(v.substr(last + 1));
    p.structured = true;
    return p;
  }
  p.given = name.child_value("GIVN");
  p.surname = name.child_value("SURN");
  p.suffix = name.child_value("NSFX");
  p.nickname = name.child_value("NICK");
  if (!p.given.empty() || !p.surname.empty()) {
    p.structured = true;
    return p;
  }
  const std::string whole = trim(v);
  if (!default_surname.empty() && whole.size() > default_surname.size() &&
      whole.compare(0, default_surname.size(), default_surname) == 0) {
    p.surname = default_surname;
    p.given = whole.substr(default_surname.size());
    p.structured = true;
    return p;
  }
  p.given = whole;
  *loss = "NAME 无 /姓/ 结构，已整串记入「名」，姓未拆分";
  return p;
}

std::string normalize_xref(std::string_view v) {
  std::string s = trim(v);
  if (s.size() >= 2 && s.front() == '@' && s.back() == '@') {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

std::string join_lines(const std::vector<std::string>& parts) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += '\n';
    out += parts[i];
  }
  return out;
}

// 按 GEDCOM xref（可带 @ 也可不带）查新分配的 id
std::string lookup(const std::unordered_map<std::string, std::string>& id_map,
                   const std::string& xref) {
  auto it = id_map.find(xref);
  if (it != id_map.end()) return it->second;
  it = id_map.find("@" + xref + "@");
  return it != id_map.end() ? it->second : std::string{};
}

// 人物 id → Person*。子女挂父母时按 id 取，写成线性扫是 O(人物×CHIL)，
// 10 万条数据直接卡死。
Person* find_person(std::vector<Person>& persons,
                    const std::unordered_map<std::string, Person*>& index,
                    const std::string& id) {
  const auto it = index.find(id);
  return it == index.end() ? nullptr : it->second;
}

struct EventTagMap {
  const char* tag;
  EventType type;
  const char* desc;
};

// GEDCOM 事实标签 → 事件类型。一对多的（洗礼/割礼等）落到 Annotation，
// 描述里写清原标签，避免信息归并丢失。
const EventTagMap kEventTags[] = {
    {"BAPM", EventType::Annotation, "洗礼"},
    {"CHR", EventType::Annotation, "受洗"},
    {"ADOP", EventType::Annotation, "收养"},
    {"EMIG", EventType::Migration, "迁出"},
    {"IMMI", EventType::Migration, "迁入"},
    {"NATU", EventType::Annotation, "归化"},
    {"OCCU", EventType::Office, "职业"},
    {"RESI", EventType::Residence, "居所"},
    {"PROP", EventType::Honor, "产业"},
    {"RELI", EventType::Annotation, "宗教信仰"},
    {"TITL", EventType::Honor, "封号"},
    {"EDUC", EventType::Examination, "受教育"},
    {"GRAD", EventType::Examination, "毕业"},
    {"ORDN", EventType::Annotation, "授职"},
    {"PROB", EventType::Annotation, "遗嘱认证"},
    {"WILL", EventType::Annotation, "遗嘱"},
    {"EVEN", EventType::Annotation, "事件"},  // 自家 Annotation 的导出标签,往返闭环(第五轮 P1-5)
};

}  // namespace

// ---------------------------------------------------------------- 映射层

// 父母链环检测（限本批次内）：从每个人物沿 father/mother 双边上溯，
// 同一 id 二次入栈即环。返回环上一个人名（空 = 无环）——谱师认名不认 ID。
// 单链走查会漏「只有经母亲边才可达」的环，故用显式栈 DFS + safe 剪枝。
std::string find_ancestry_cycle(const std::vector<Person>& persons) {
  std::unordered_map<std::string, const Person*> by_id;
  by_id.reserve(persons.size() * 2);
  for (const auto& p : persons) by_id.emplace(p.id, &p);
  std::unordered_set<std::string> safe;   // 已确认无环，剪枝用
  for (const auto& start : persons) {
    if (safe.count(start.id) > 0) continue;
    std::vector<std::pair<std::string, int>> stack;   // (id, 下一条父母边)
    std::unordered_set<std::string> on_path;
    stack.emplace_back(start.id, 0);
    on_path.insert(start.id);
    bool cycle = false;
    std::string culprit;
    while (!stack.empty() && !cycle) {
      // 取值不用引用：emplace 可能令 vector 重分配、引用失效
      const std::string id = stack.back().first;
      int idx = stack.back().second;
      const auto it = by_id.find(id);
      if (it == by_id.end()) {
        on_path.erase(id);
        safe.insert(id);
        stack.pop_back();
        continue;
      }
      const Person* p = it->second;
      const std::string parents[2] = {
          p->father_id ? *p->father_id : std::string{},
          p->mother_id ? *p->mother_id : std::string{}};
      bool advanced = false;
      while (idx < 2) {
        const std::string nxt = parents[idx++];
        stack.back().second = idx;
        if (nxt.empty() || by_id.count(nxt) == 0) continue;
        if (on_path.count(nxt) > 0) {
          cycle = true;
          culprit = nxt;
          break;
        }
        if (safe.count(nxt) > 0) continue;
        stack.emplace_back(nxt, 0);
        on_path.insert(nxt);
        advanced = true;
        break;
      }
      if (cycle) break;
      if (!advanced) {
        on_path.erase(id);
        safe.insert(id);
        stack.pop_back();
      }
    }
    if (cycle) {
      const auto cit = by_id.find(culprit);
      const std::string name = cit != by_id.end()
                                   ? cit->second->family_name +
                                         cit->second->given_name
                                   : culprit;
      return name + "（父母链成环）";
    }
  }
  return {};
}

std::vector<Person> order_by_dependency(const std::vector<Person>& all) {
  // in_batch 用哈希集合：这个函数会被 10 万条数据的灌库路径调用，
  // 线性扫一遍是 O(n^2)，实测直接卡死。
  std::unordered_set<std::string> in_batch;
  in_batch.reserve(all.size() * 2);
  for (const auto& p : all) in_batch.insert(p.id);
  auto depends_on_batch = [&in_batch](const std::optional<std::string>& id) {
    return id && in_batch.count(*id) > 0;
  };
  std::vector<Person> pending = all;
  std::vector<Person> ordered;
  std::unordered_set<std::string> created;
  while (!pending.empty()) {
    const std::size_t before = pending.size();
    for (auto it = pending.begin(); it != pending.end();) {
      const bool father_ok =
          !depends_on_batch(it->father_id) || created.count(*it->father_id) > 0;
      const bool mother_ok =
          !depends_on_batch(it->mother_id) || created.count(*it->mother_id) > 0;
      if (father_ok && mother_ok) {
        ordered.push_back(*it);
        created.insert(it->id);
        it = pending.erase(it);
      } else {
        ++it;
      }
    }
    if (pending.size() == before) {
      // 环或自指：排不出来。按 id 排序硬灌，让用例层报出具体的错——
      // 静默跳过会让谱师以为导全了
      std::sort(pending.begin(), pending.end(),
                [](const Person& a, const Person& b) { return a.id < b.id; });
      for (const auto& p : pending) ordered.push_back(p);
      break;
    }
  }
  return ordered;
}

Result<ImportResult> import_gedcom(const GedcomDocument& doc,
                                   const ImportOptions& opt) {
  ImportResult res;
  const std::string now = utc_now_iso();
  std::unordered_map<std::string, std::string> place_ids;   // 地名 → id

  auto add_loss = [&](LossSeverity sev, const std::string& xref,
                      const std::string& tag, const std::string& reason) {
    res.losses.push_back(MappingLoss{sev, xref, tag, reason});
  };

  // 地点按名称去重（GEDCOM 没有 PLAC 记录，地点是内联字符串）
  auto place_for = [&](const std::string& name, const GedcomNode* plac_node,
                       const std::string& xref) -> std::string {
    if (name.empty()) return {};
    auto it = place_ids.find(name);
    if (it != place_ids.end()) return it->second;
    if (!opt.create_places) {
      add_loss(LossSeverity::Warning, xref, "PLAC",
               "已关闭自动建地点，" + name + " 未落库");
      return {};
    }
    Place pl;
    pl.id = generate_ulid();
    pl.name = name;
    pl.type = PlaceType::Other;
    if (plac_node) {
      if (const auto* map = plac_node->child("MAP")) {
        const auto parse_latlon = [](const std::string& s, double* out) {
          if (s.empty()) return false;
          std::size_t i = 0;
          double sign = 1.0;
          if (s[0] == 'N' || s[0] == 'E') { sign = 1.0; i = 1; }
          else if (s[0] == 'S' || s[0] == 'W') { sign = -1.0; i = 1; }
          if (i >= s.size()) return false;
          try {
            std::size_t used = 0;
            *out = sign * std::stod(s.substr(i), &used);
            return used == s.size() - i;
          } catch (const std::exception&) {
            return false;
          }
        };
        double v = 0.0;
        if (const auto* lati = map->child("LATI")) {
          if (parse_latlon(trim(lati->value), &v)) pl.lat = v;
        }
        if (const auto* lng = map->child("LONG")) {
          if (parse_latlon(trim(lng->value), &v)) pl.lng = v;
        }
      }
    }
    place_ids.emplace(name, pl.id);
    res.places.push_back(pl);
    return pl.id;
  };

  // ---- 第零遍：0 @Sx@ SOUR 来源记录（M7.5 出处链）----
  // TITL→title、AUTH→author、PUBL→repository、PUBB→year_text（部分老谱
  // 用的非标 tag，语义即刊行年份照录）、TEXT→citation_text、NOTE→note。
  // 缺 TITL 仍建档（占位标题），否则引用它的 citation 全部悬空；未映射
  // 子字段逐条 Info loss——损失显式化纪律不因映射而放松。
  std::unordered_map<std::string, std::string> source_ids;  // @S1@ → id
  // 共享 NOTE 记录文本（第五轮 P2-10）：`0 @N1@ NOTE` 形态此前不解析，
  // INDI 级 `1 NOTE @N1@` 的指针串被原样塞进备注字段。
  std::unordered_map<std::string, std::string> shared_notes;  // @N1@ → 文本
  for (const auto& rec : doc.records) {
    if (rec.tag != "NOTE") continue;
    if (!rec.xref.empty()) {
      shared_notes.emplace("@" + rec.xref + "@", rec.value);
    }
  }
  for (const auto& rec : doc.records) {
    if (rec.tag != "SOUR") continue;
    const std::string sxref = "@" + rec.xref + "@";
    Source s;
    s.id = generate_ulid();
    s.title = trim(rec.child_value("TITL"));
    if (s.title.empty()) s.title = trim(rec.child_value("TITLE"));
    if (s.title.empty()) {
      s.title = "(未命名来源 " + sxref + ")";
      add_loss(LossSeverity::Info, sxref, "SOUR",
               "来源记录缺 TITLE，已用占位标题建档");
    }
    if (const auto* a = rec.child("AUTH")) {
      const std::string v = trim(a->value);
      if (!v.empty()) s.author = v;
    }
    if (const auto* p = rec.child("PUBL")) {
      const std::string v = trim(p->value);
      if (!v.empty()) s.repository = v;
    }
    if (const auto* p = rec.child("PUBB")) {
      const std::string v = trim(p->value);
      if (!v.empty()) s.year_text = v;
    }
    if (const auto* t = rec.child("TEXT")) {
      const std::string v = trim(t->value);
      if (!v.empty()) s.citation_text = v;
    }
    if (const auto* n = rec.child("NOTE")) {
      const std::string v = trim(n->value);
      if (!v.empty()) s.note = v;
    }
    for (const auto& ch : rec.children) {
      if (ch.tag == "TITL" || ch.tag == "TITLE" || ch.tag == "AUTH" ||
          ch.tag == "PUBL" || ch.tag == "PUBB" || ch.tag == "TEXT" ||
          ch.tag == "NOTE") {
        continue;
      }
      add_loss(LossSeverity::Info, sxref, "SOUR." + ch.tag,
               "来源记录子字段 " + ch.tag + " 无对应字段，未映射");
    }
    const std::string sid = s.id;  // 先留底：move 后读 s.id 是空串（ULID 26 字符超 SSO）
    if (!source_ids.emplace(sxref, sid).second) {
      add_loss(LossSeverity::Warning, sxref, "SOUR",
               "重复的来源标识（文件内出现多次），保留第一条");
      continue;
    }
    res.sources.push_back(std::move(s));
    res.id_map.emplace(sxref, sid);
  }

  // QUAY ↔ confidence（双向映射的导入半边）：3=direct/primary→high、
  // 2=secondary→medium、1=questionable→low、0=unreliable→disputed
  // （disputed 取其「不可信/待裁定」一半，换往返对称）；缺省=medium
  // （M7.2 既定默认），非法值照缺省——宁可保守不猜。
  auto quay_to_confidence = [&](const GedcomNode* sour_node) -> Confidence {
    const auto* q = sour_node == nullptr ? nullptr : sour_node->child("QUAY");
    if (q == nullptr) return Confidence::Medium;
    const std::string v = trim(q->value);
    if (v == "3") return Confidence::High;
    if (v == "2") return Confidence::Medium;
    if (v == "1") return Confidence::Low;
    if (v == "0") return Confidence::Disputed;
    return Confidence::Medium;
  };

  // SOUR 结构（1 SOUR / 2 SOUR 节点）→ Citation。值是 @xref@ 形式时查
  // 第零遍的来源表；非 @ 形式 = inline 来源（整段文本即出处描述，如
  // 「1 SOUR 家谱软件测试数据生成器」），隐式建 Source。PAGE→locator、
  // EVEN/DATA.TEXT→note。
  // 指针判定收紧（第五轮 P2-9）：xref 须为非空 ASCII 字母数字，纯文本
  // "@标题@" 形态不再被误判成断链指针。
  auto is_gedcom_pointer = [](const std::string& v) {
    if (v.size() < 3 || v.front() != '@' || v.back() != '@') return false;
    const std::string inner = v.substr(1, v.size() - 2);
    if (inner.empty()) return false;
    for (const char ch : inner) {
      if (!std::isalnum(static_cast<unsigned char>(ch))) return false;
    }
    return true;
  };
  auto citation_from_sour = [&](const GedcomNode& sour,
                                const std::string& subject_xref,
                                CitationSubjectType st,
                                const std::string& subject_id) {
    Citation c;
    c.id = generate_ulid();
    c.subject_type = st;
    c.subject_id = subject_id;
    const std::string val = trim(sour.value);
    if (is_gedcom_pointer(val)) {
      const auto it = res.id_map.find(val);
      if (it == res.id_map.end()) {
        add_loss(LossSeverity::Warning, subject_xref, "SOUR",
                 "引用了未定义的来源记录 " + val + "，出处链断链");
        return;
      }
      c.source_id = it->second;
    } else if (!val.empty()) {
      Source s;
      s.id = generate_ulid();
      s.title = val;
      const std::string sid = s.id;  // 同上：先留底再 move
      res.sources.push_back(std::move(s));
      c.source_id = sid;
    } else {
      add_loss(LossSeverity::Warning, subject_xref, "SOUR",
               "SOUR 结构既无指向也无文本，无法建档");
      return;
    }
    if (const auto* pg = sour.child("PAGE")) {
      const std::string loc = trim(pg->value);
      if (!loc.empty()) c.locator = loc;
    }
    c.confidence = quay_to_confidence(&sour);
    std::vector<std::string> bits;
    if (const auto* ev = sour.child("EVEN")) {
      const std::string v = trim(ev->value);
      if (!v.empty()) bits.push_back("EVEN: " + v);
    }
    if (const auto* data = sour.child("DATA")) {
      if (const auto* t = data->child("TEXT")) {
        const std::string v = trim(t->value);
        if (!v.empty()) bits.push_back("TEXT: " + v);
      }
    }
    if (const auto* n = sour.child("NOTE")) {
      const std::string v = trim(n->value);
      if (!v.empty()) bits.push_back(v);  // 导出把 note 写成 2 NOTE
    }
    if (!bits.empty()) c.note = join_lines(bits);
    res.citations.push_back(std::move(c));
  };

  // ---- 第一遍：INDI ----
  for (const auto& rec : doc.records) {
    if (rec.tag != "INDI") continue;
    const std::string xref = "@" + rec.xref + "@";
    Person p;
    p.id = generate_ulid();
    p.created_at = now;
    p.updated_at = now;

    if (const auto* n = rec.child("NAME")) {
      std::string loss;
      const NameParts parts = parse_person_name(*n, opt.default_surname, &loss);
      if (!loss.empty()) add_loss(LossSeverity::Info, xref, "NAME", loss);
      p.family_name = parts.surname.empty() ? opt.default_surname : parts.surname;
      p.given_name = parts.given;
      if (!parts.suffix.empty()) p.alias = parts.suffix;
      if (!parts.nickname.empty()) {
        if (p.alias) {
          *p.alias += "/" + parts.nickname;
        } else {
          p.alias = parts.nickname;
        }
      }
      if (p.family_name.empty()) {
        add_loss(LossSeverity::Warning, xref, "NAME",
                 "姓为空（GEDCOM 无 /姓/ 结构且未提供默认姓），建档会被用例层拒绝");
      }
    } else {
      p.family_name = opt.default_surname;
      add_loss(LossSeverity::Warning, xref, "NAME", "该人物没有 NAME 记录");
    }

    const std::string sex = upper_ascii(trim(rec.child_value("SEX")));
    if (sex == "M") {
      p.gender = Gender::Male;
    } else if (sex == "F") {
      p.gender = Gender::Female;
    } else if (sex.empty() || sex == "U") {
      p.gender = Gender::Unknown;
      add_loss(LossSeverity::Info, xref, "SEX", "性别未记录");
    } else {
      p.gender = Gender::Unknown;
      add_loss(LossSeverity::Warning, xref, "SEX", "性别取值无法识别: " + sex);
    }

    // 事实标签统一走一个出口：日期 + 地点 + 事件
    auto handle_fact = [&](const char* fact_tag, EventType ev_type,
                           const char* ev_desc) {
      const auto* node = rec.child(fact_tag);
      if (node == nullptr) return;
      // GEDCOM 5.5.1 的 EVENT_Y_OR_N：Y（或空值，两者同义）= 事件已发生，
      // N = 未发生。"1 DEAT Y" 的标准语义是「死亡事件已发生、日期未知」
      // ——此前读成「在世」把生死读反了。凡出现 DEAT 事实即此人已故；
      // 显式 N（未死亡）则在世保护照旧。Y 与裸值走同一条通用路径：
      // 日期照常解析（Y 本无日期）、地点照常落 death_place_id——
      // 曾把 Y 提前返回，`1 DEAT Y` + `2 PLAC` 的地点被静默丢弃，
      // 且裸 DEAT 建事件而 DEAT Y 不建，同一语义两种库内状态。
      const std::string fact_val = upper_ascii(trim(node->value));
      if (ev_type == EventType::Death) {
        if (fact_val == "N") {
          p.is_living = true;   // 显式在世：恢复在世保护标记
          // DEAT N 不建事件（无事实可引），但其下内容不能静默丢
          if (node->child("DATE") != nullptr) {
            add_loss(LossSeverity::Info, xref, "DEAT.DATE",
                     "「未死亡」事实的日期无处挂接，已按损失记账");
          }
          for (const auto* s : node->children_all("SOUR")) {
            add_loss(LossSeverity::Info, xref, "SOUR",
                     "「未死亡」事实不建事件，其下的出处引用无处挂接，"
                     "已按损失记账");
            (void)s;
          }
          return;
        }
        p.is_living = false;
      }
      DateOutcome o;
      if (const auto* d = node->child("DATE")) {
        o = parse_gedcom_date(d->value);
        if (!o.note.empty()) {
          add_loss(LossSeverity::Warning, xref, std::string(fact_tag) + ".DATE",
                   o.note);
        }
        if (ev_type == EventType::Birth) {
          p.birth_date = o.date;
        } else if (ev_type == EventType::Death) {
          p.death_date = o.date;
        }
      }
      Event e;
      e.id = generate_ulid();
      e.subject_type = EventSubjectType::Person;
      e.subject_id = p.id;
      e.type = ev_type;
      e.created_at = now;
      if (const auto* d = node->child("DATE")) e.date = o.date;
      if (ev_desc && *ev_desc) e.description = ev_desc;
      // 事实行主值载荷（"1 OCCU 农民" 的"农民"）此前被静默丢弃（第五轮 P1-1）。
      // Y/N/空值（BIRT/DEAT 类）无载荷，跳过。
      {
        const std::string payload = trim(node->value);
        if (!payload.empty() && payload != "Y" && payload != "N") {
          if (e.description && !e.description->empty())
            *e.description += "：" + payload;
          else
            e.description = payload;
        }
      }
      // 事实级子结构：NOTE/CAUSE/AGE/ADDR/TYPE 并入事件描述，其余一律
      // 记账——「认不出的东西绝不静默丢弃」（第五轮 P1-1/P1-5）
      {
        std::string extra;
        auto append = [&](const std::string& label, const std::string& v) {
          if (v.empty()) return;
          if (!extra.empty()) extra += "；";
          extra += label + v;
        };
        for (const auto* n : node->children_all("NOTE")) {
          append("注：", n->value);
        }
        if (const auto* c = node->child("CAUSE"))
          append("原因：", trim(c->value));
        if (const auto* c = node->child("AGE")) append("年龄：", trim(c->value));
        if (const auto* c = node->child("ADDR")) append("地址：", trim(c->value));
        if (const auto* c = node->child("TYPE")) append("类型：", trim(c->value));
        if (!extra.empty()) {
          if (e.description && !e.description->empty())
            *e.description += "；" + extra;
          else
            e.description = extra;
        }
        static const std::unordered_set<std::string> kRouted = {
            "DATE", "PLAC", "SOUR", "NOTE", "CAUSE", "AGE", "ADDR", "TYPE"};
        for (const auto& c : node->children) {
          if (kRouted.count(c.tag) == 0) {
            add_loss(LossSeverity::Info, xref,
                     std::string(fact_tag) + "." + c.tag,
                     "事实行下的未路由子结构，已按损失记账");
          }
        }
      }
      res.events.push_back(e);
      if (const auto* pl = node->child("PLAC")) {
        const std::string name = trim(pl->value);
        const std::string pid = place_for(name, pl, xref);
        if (!pid.empty()) {
          if (ev_type == EventType::Birth) {
            p.birth_place_id = pid;
          } else if (ev_type == EventType::Death) {
            p.death_place_id = pid;
          } else if (ev_type == EventType::Burial) {
            p.burial_place_id = pid;
          } else {
            e.place_id = pid;
            res.events.back().place_id = pid;
          }
        }
        // 地点级 3 SOUR/NOTE 一并路由（第五轮 P2-11）
        for (const auto* s : pl->children_all("SOUR")) {
          citation_from_sour(*s, xref, CitationSubjectType::Event,
                             res.events.back().id);
        }
        for (const auto* n : pl->children_all("NOTE")) {
          const std::string v = trim(n->value);
          if (!v.empty()) {
            Event& back = res.events.back();
            if (back.description && !back.description->empty())
              *back.description += "；注：" + v;
            else
              back.description = v;
          }
        }
      }
      // 事实级 2 SOUR → 挂到该事实创建的事件（M7.5 出处链）
      for (const auto* s : node->children_all("SOUR")) {
        citation_from_sour(*s, xref, CitationSubjectType::Event, e.id);
      }
    };

    handle_fact("BIRT", EventType::Birth, "");
    handle_fact("DEAT", EventType::Death, "");
    handle_fact("BURI", EventType::Burial, "");
    for (const auto& m : kEventTags) {
      handle_fact(m.tag, m.type, m.desc);
    }

    // 备注（第五轮 P2-10）：指针形态解析共享 NOTE 记录，未命中记账，
    // 不再把 "@N1@" 字面串塞进备注字段
    std::vector<std::string> notes;
    for (const auto* n : rec.children_all("NOTE")) {
      if (!n->value.empty() && n->value.front() == '@' &&
          n->value.back() == '@' && n->value.size() >= 3) {
        const auto it = shared_notes.find(n->value);
        if (it != shared_notes.end()) {
          if (!it->second.empty()) notes.push_back(it->second);
        } else {
          add_loss(LossSeverity::Warning, xref, "NOTE",
                   "引用了未定义的共享备注记录 " + n->value + "，正文缺失");
        }
        continue;
      }
      notes.push_back(n->value);
    }
    if (const auto* refn = rec.child("REFN")) {
      const std::string v = trim(refn->value);
      if (!v.empty()) {
        notes.push_back("[编号] " + v);
        add_loss(LossSeverity::Info, xref, "REFN",
                 "家谱编号无专属字段，已并入备注");
      }
    }
    if (!notes.empty()) p.note = join_lines(notes);

    if (const auto* alia = rec.child("ALIA")) {
      const std::string v = trim(alia->value);
      if (!v.empty()) p.alias = p.alias ? *p.alias + "/" + v : v;
    }
    if (const auto* chan = rec.child("CHAN")) {
      const std::string v = trim(chan->child_value("DATE"));
      // GEDCOM 文法的日期时间转回 ISO 存库；其他写法（第三方）原样照录
      if (!v.empty()) p.updated_at = gedcom_datetime_to_iso(v);
    }
    for (const auto* obje : rec.children_all("OBJE")) {
      (void)obje;
      add_loss(LossSeverity::Warning, xref, "OBJE",
               "多媒体附件未建模（M1 无附件库），链接已丢失");
    }
    // INDI 级 1 SOUR（无事实上下文）→ Person 级 citation（M7.5）；
    // 多条 SOUR 全部收集（此前只看第一条）
    for (const auto* sour : rec.children_all("SOUR")) {
      citation_from_sour(*sour, xref, CitationSubjectType::Person, p.id);
    }
    for (const auto* uid : rec.children_all("_UID")) {
      (void)uid;
      add_loss(LossSeverity::Info, xref, "_UID",
               "外部系统唯一号无对应字段，已忽略");
    }
    for (const auto* asso : rec.children_all("ASSO")) {
      (void)asso;
      add_loss(LossSeverity::Info, xref, "ASSO",
               "关联人（ASSO）关系未建模，已忽略");
    }

    // INDI 记录级兜底（第五轮 P1-5）：未识别的一级子标签一律记账，
    // 绝不静默丢弃（第三方自定义标签、收养标记 FAMC.PEDI 等）。
    {
      static const std::unordered_set<std::string> kIndiKnown = [] {
        std::unordered_set<std::string> known = {
            "NAME", "SEX",  "BIRT", "DEAT", "BURI", "NOTE", "REFN",
            "ALIA", "CHAN", "OBJE", "SOUR", "_UID", "ASSO", "FAMS",
            "FAMC"};
        for (const auto& m : kEventTags) known.insert(m.tag);
        return known;
      }();
      for (const auto& c : rec.children) {
        if (kIndiKnown.count(c.tag) == 0) {
          add_loss(LossSeverity::Info, xref, c.tag,
                   "未识别的人物记录子标签，已按损失记账");
        }
      }
    }

    // 重复 xref：emplace 不覆盖是刻意的（保留第一条），但必须记账——
    // 此前静默跳过，后续所有同 xref 的 FAM 引用都指向首条而无人知晓。
    if (!res.id_map.emplace(xref, p.id).second) {
      add_loss(LossSeverity::Warning, xref, "XREF",
               "重复的人物标识（文件内出现多次），保留第一条，"
               "后续同 xref 引用都指向首条");
    }
    res.persons.push_back(std::move(p));
  }

  // ---- 第二遍：FAM（此时所有人都已建档，可解引用） ----
  std::unordered_map<std::string, Person*> person_by_id;
  person_by_id.reserve(res.persons.size() * 2);
  for (auto& p : res.persons) person_by_id.emplace(p.id, &p);
  std::unordered_map<std::string, int> union_count_per_person;  // person id → 结合数
  struct FamRaw {
    std::string xref;
    std::string husb;
    std::string wife;
    std::vector<std::string> conc;   // 妾/继配（扩展标签 _CONC）
    std::vector<std::string> chil;
    const GedcomNode* marr = nullptr;
    const GedcomNode* div = nullptr;
    const GedcomNode* note = nullptr;
    const GedcomNode* node = nullptr;  // 整记录节点：FAM 级 1 SOUR 取用
    bool has_obje = false;
  };
  std::vector<FamRaw> fams;
  std::unordered_set<std::string> fam_xrefs_seen;
  for (const auto& rec : doc.records) {
    if (rec.tag != "FAM") continue;
    FamRaw f;
    f.xref = "@" + rec.xref + "@";
    // 重复 FAM xref：与 INDI 重复同一纪律——不静默。结合 id 本就是
    // 生成的 ULID，两份都会导入，但谱师必须知道文件里有重复标识。
    if (!fam_xrefs_seen.insert(f.xref).second) {
      add_loss(LossSeverity::Warning, f.xref, "XREF",
               "重复的结合标识（文件内出现多次），两份都会导入为独立结合");
    }
    if (const auto* h = rec.child("HUSB")) f.husb = normalize_xref(h->value);
    if (const auto* w = rec.child("WIFE")) f.wife = normalize_xref(w->value);
    // 妾/继配：GEDCOM 5.5.1 无此角色，自家导出写扩展标签 _CONC
    for (const auto* c : rec.children_all("_CONC")) {
      const std::string x = normalize_xref(c->value);
      if (!x.empty()) f.conc.push_back(x);
    }
    for (const auto* c : rec.children_all("CHIL")) {
      const std::string x = normalize_xref(c->value);
      if (!x.empty()) f.chil.push_back(x);
    }
    f.marr = rec.child("MARR");
    f.div = rec.child("DIV");
    f.note = rec.child("NOTE");
    f.node = &rec;
    f.has_obje = !rec.children_all("OBJE").empty();
    fams.push_back(std::move(f));
  }
  // 任次统计按 person id：GEDCOM xref 与领域 id 不是一套键
  for (const auto& f : fams) {
    for (const std::string* who : {&f.husb, &f.wife, }) {
      if (who->empty()) continue;
      const std::string id = lookup(res.id_map, *who);
      if (!id.empty()) ++union_count_per_person[id];
    }
  }

  for (const auto& f : fams) {
    const std::string husb_id = f.husb.empty() ? std::string{} : lookup(res.id_map, f.husb);
    const std::string wife_id = f.wife.empty() ? std::string{} : lookup(res.id_map, f.wife);
    std::vector<std::string> conc_ids;
    for (const auto& cx : f.conc) {
      const std::string id = lookup(res.id_map, cx);
      if (!id.empty()) {
        conc_ids.push_back(id);
      } else {
        add_loss(LossSeverity::Warning, f.xref, "_CONC",
                 "引用了未建档的妾/继配 " + cx);
      }
    }
    if (husb_id.empty() && wife_id.empty() && conc_ids.empty()) {
      add_loss(LossSeverity::Warning, f.xref, "FAM",
               "HUSB/WIFE 均缺失或引用了未建档人物，该结合未导入");
      continue;
    }
    if (!f.husb.empty() && husb_id.empty()) {
      add_loss(LossSeverity::Warning, f.xref, "HUSB", "引用了文件中不存在的丈夫");
    }
    if (!f.wife.empty() && wife_id.empty()) {
      add_loss(LossSeverity::Warning, f.xref, "WIFE", "引用了文件中不存在的妻子");
    }

    // 单亲家庭（只有 HUSB 或只有 WIFE + CHIL，无配偶无妾）：FAM 本义
    // 是家庭单位而非婚姻——只还原父/母链接，不造单成员的结合。
    // （自家单亲合成 FAM 与第三方单亲文件都走这条路径。）
    const bool single_parent_family =
        wife_id.empty() && conc_ids.empty() && f.marr == nullptr;

    Union u;
    u.id = generate_ulid();
    u.rev = 1;
    // 无正室而有妾成员的结合 = 妾室结合（自家 _CONC 往返恢复 type；
    // 第三方文件不会有 _CONC，不受影响）
    if (wife_id.empty() && !conc_ids.empty()) {
      u.type = UnionType::Concubinage;
    }
    if (!husb_id.empty()) u.members.push_back(UnionMember{husb_id, MemberRole::Husband});
    if (!wife_id.empty()) u.members.push_back(UnionMember{wife_id, MemberRole::Wife});
    for (const auto& cid : conc_ids) {
      u.members.push_back(UnionMember{cid, MemberRole::Concubine});
    }

    // 子女：反范式冗余 father_id/mother_id（权威关系在 union_member，
    // 一致性由校验规则 R7 兜底）
    for (const auto& cx : f.chil) {
      const auto it = res.id_map.find("@" + cx + "@");
      if (it == res.id_map.end()) {
        add_loss(LossSeverity::Warning, f.xref, "CHIL",
                 "引用了未建档的子女 " + cx);
        continue;
      }
      Person* child = find_person(res.persons, person_by_id, it->second);
      if (child == nullptr) continue;
      if (!husb_id.empty() && !child->father_id) {
        child->father_id = husb_id;
        const auto* hp = find_person(res.persons, person_by_id, husb_id);
        if (hp && hp->gender == Gender::Unknown) {
          add_loss(LossSeverity::Info, f.xref, "CHIL",
                   "丈夫性别未知，按 GEDCOM HUSB 角色推定为父亲");
        }
      }
      if (!wife_id.empty() && !child->mother_id) {
        child->mother_id = wife_id;
        const auto* wp = find_person(res.persons, person_by_id, wife_id);
        if (wp && wp->gender == Gender::Unknown) {
          add_loss(LossSeverity::Info, f.xref, "CHIL",
                   "妻子性别未知，按 GEDCOM WIFE 角色推定为母亲");
        }
      }
      // 妾室结合无正室：子女的母位按唯一妾成员恢复——不是谱学判断
      // （数据里该结合只有一位母位候选），否则妾出子女的母链往返丢失
      // （高阳李氏试修实证：东涵母夏氏）。
      if (wife_id.empty() && conc_ids.size() == 1 && !child->mother_id) {
        child->mother_id = conc_ids[0];
      }
    }

    if (single_parent_family) {
      // 纯亲子 FAM：父母链接已在上面的循环里还原，不需要结合记录
      continue;
    }

    std::string marr_notes;
    if (f.marr) {
      std::optional<DateOutcome> marr_date;
      if (const auto* d = f.marr->child("DATE")) {
        const DateOutcome o = parse_gedcom_date(d->value);
        u.start_date = o.date;
        marr_date = o;
        if (!o.note.empty()) {
          add_loss(LossSeverity::Warning, f.xref, "MARR.DATE", o.note);
        }
      }
      Event e;
      e.id = generate_ulid();
      e.subject_type = EventSubjectType::Union;
      e.subject_id = u.id;
      e.type = EventType::Marriage;
      if (marr_date) e.date = marr_date->date;
      e.created_at = now;
      // PLAC/SOUR 不再嵌在 DATE 分支内（第五轮 P1-2）：婚姻无日期但
      // 知地点/有证据时，此前整块静默丢弃。
      if (const auto* pl = f.marr->child("PLAC")) {
        const std::string pid = place_for(trim(pl->value), pl, f.xref);
        // 地点写回结合（第五轮 P1-3）：此前只写事件、导出侧读 u.place_id，
        // 婚姻地点往返必丢。
        if (!pid.empty()) {
          e.place_id = pid;
          u.place_id = pid;
        }
      }
      res.events.push_back(e);
      // MARR 事实级 2 SOUR → 挂到婚姻事件（M7.5）
      for (const auto* s : f.marr->children_all("SOUR")) {
        citation_from_sour(*s, f.xref, CitationSubjectType::Event, e.id);
      }
      // MARR 级 NOTE 并入结合备注（第五轮 P2-11 同口）
      for (const auto* n : f.marr->children_all("NOTE")) {
        const std::string v = trim(n->value);
        if (!v.empty()) {
          if (!marr_notes.empty()) marr_notes += "\n";
          marr_notes += v;
        }
      }
      // MARR 子结构兜底
      static const std::unordered_set<std::string> kMarrRouted = {
          "DATE", "PLAC", "SOUR", "NOTE", "TYPE"};
      for (const auto& c : f.marr->children) {
        if (kMarrRouted.count(c.tag) == 0) {
          add_loss(LossSeverity::Info, f.xref,
                   std::string("MARR.") + c.tag,
                   "婚姻事实下的未路由子结构，已按损失记账");
        }
      }
    }
    if (f.div) {
      if (const auto* d = f.div->child("DATE")) {
        const DateOutcome o = parse_gedcom_date(d->value);
        u.end_date = o.date;
        if (!o.note.empty()) {
          add_loss(LossSeverity::Warning, f.xref, "DIV.DATE", o.note);
        }
      }
      // DIV 的证据挂到结合（第五轮 P1-2：DIV.SOUR 此前完全未路由）
      for (const auto* s : f.div->children_all("SOUR")) {
        citation_from_sour(*s, f.xref, CitationSubjectType::Union, u.id);
      }
      for (const auto* n : f.div->children_all("NOTE")) {
        const std::string v = trim(n->value);
        if (!v.empty()) {
          if (!marr_notes.empty()) marr_notes += "\n";
          marr_notes += v;
        }
      }
      u.status = UnionStatus::Dissolved;
      add_loss(LossSeverity::Info, f.xref, "DIV",
               "离异事件无专属类型，已记为结合状态 + 结束日期");
    }
    if (f.note) u.note = f.note->value;
    if (!marr_notes.empty()) {
      u.note = u.note ? *u.note + "\n" + marr_notes : marr_notes;
    }
    if (f.has_obje) {
      add_loss(LossSeverity::Warning, f.xref, "OBJE",
               "多媒体附件未建模（M1 无附件库），链接已丢失");
    }
    // FAM 级 1 SOUR → Union 级 citation（M7.5 出处链）
    if (f.node != nullptr) {
      for (const auto* s : f.node->children_all("SOUR")) {
        citation_from_sour(*s, f.xref, CitationSubjectType::Union, u.id);
      }
      // FAM 记录级兜底（第五轮 P1-5）：未识别的一级子标签一律记账
      static const std::unordered_set<std::string> kFamKnown = {
          "HUSB", "WIFE", "_CONC", "CHIL", "MARR",
          "DIV",  "NOTE", "SOUR",  "OBJE"};
      for (const auto& c : f.node->children) {
        if (kFamKnown.count(c.tag) == 0) {
          add_loss(LossSeverity::Info, f.xref, c.tag,
                   "未识别的结合记录子标签，已按损失记账");
        }
      }
    }
    res.unions.push_back(std::move(u));
  }

  // 任次：同一人出现在多个结合里时，按出现顺序编号（第几任）
  {
    std::unordered_map<std::string, int> seen;
    for (auto& u : res.unions) {
      for (auto& m : u.members) {
        const auto it = union_count_per_person.find(m.person_id);
        if (it == union_count_per_person.end() || it->second <= 1) continue;
        const int seq = ++seen[m.person_id];
        u.sequence = seq;
      }
    }
  }

  // 未支持的记录类型
  for (const auto& rec : doc.records) {
    // HEAD/SUBM/SUBN/TRLR 是文件框架而非谱面信息，无领域模型但也不是
    // 「丢失的信息」；记 Info 只会淹没真正要人看的损失，故直接跳过。
    // SOUR 于 M7.5 建模（第零遍），同样移出兜底。
    if (rec.tag == "INDI" || rec.tag == "FAM" || rec.tag == "HEAD" ||
        rec.tag == "SUBM" || rec.tag == "SUBN" || rec.tag == "TRLR" ||
        rec.tag == "SOUR") {
      continue;
    }
    add_loss(LossSeverity::Info, "@" + rec.xref + "@", rec.tag,
             "记录类型 " + rec.tag + " 暂无对应模型，未导入");
  }

  // 世系环检测（落库前最后一道）：父母链上溯回到自己，含互环。
  // order_by_dependency 对环只能按 id 硬灌，落库时报「父亲不存在」——
  // 对谱师是误导性错误，真正的病因是环。映射层拦下并报出环上人名。
  if (const std::string cyc = find_ancestry_cycle(res.persons); !cyc.empty()) {
    return invalid_argument("世系环，无法导入: " + cyc);
  }
  return res;
}

// ---------------------------------------------------------------- 写出层
// 约束：单行不超过 255 字符（GEDCOM 规范），超长按 CONC 切、且必须在
// UTF-8 字符边界上切；文本里的换行走 CONT。这两条保证自家读取器
// 能无损读回（往返测试见 test_gedcom.cpp）。
// 7.0 差异（M9.3）：7.0 删除了 CONC 标签、且不再约束行长——7.0 写出
// 走单行长行（allow_conc=false），CONT 换行语义不变。

namespace {

class LineWriter {
 public:
  explicit LineWriter(bool allow_conc = true) : allow_conc_(allow_conc) {}

  void raw(int level, const std::string& body) {
    const std::string prefix = std::to_string(level) + " ";
    if (!allow_conc_) {
      // 7.0：无 CONC，超长直接单行长行（7.0 不约束行长；自家读取器
      // split_line 不限长，能无损读回）
      out_ += prefix + body + "\n";
      return;
    }
    if (prefix.size() + body.size() <= 255) {
      out_ += prefix + body + "\n";
      return;
    }
    // 续行必须比正文深一级：GEDCOM 的 CONC/CONT 是正文节点的子节点。
    // 写成同级会让解析器把它当成兄弟，备注只剩第一段（踩过）。
    const std::string cont_prefix = std::to_string(level + 1) + " CONC ";
    std::size_t start = 0;
    bool first = true;
    while (start < body.size()) {
      const std::size_t avail =
          first ? (255 - prefix.size()) : (255 - cont_prefix.size());
      std::size_t end = std::min(body.size(), start + avail);
      if (end < body.size()) end = utf8_back(body, end, start);
      // 续字节卡死兜底（第五轮 P3-1）：切分点起全是续字节时 utf8_back
      // 会退回 start，循环不前进——强制按字节前进，宁出坏行不挂死。
      if (end <= start) end = std::min(body.size(), start + avail);
      out_ += (first ? prefix : cont_prefix) + body.substr(start, end - start) +
              "\n";
      start = end;
      first = false;
    }
  }

  // 带可能含换行的文本值：首段作 value，其余段作 CONT。
  // GEDCOM @ 转义（第五轮 P2-9）：值里的 @ 写作 @@，导入侧 split_line
  // 对称解出。指针行（SOUR @S1@ 等）不走 text()，不受影响。
  void text(int level, const std::string& tag, const std::string& value) {
    std::string esc;
    esc.reserve(value.size());
    for (const char c : value) {
      esc += c;
      if (c == '@') esc += '@';
    }
    std::vector<std::string> segs;
    std::string cur;
    for (const char c : esc) {
      if (c == '\n') {
        segs.push_back(cur);
        cur.clear();
      } else {
        cur += c;
      }
    }
    segs.push_back(cur);
    if (segs.empty()) segs.push_back(std::string{});
    raw(level, tag + (segs[0].empty() ? std::string{} : " " + segs[0]));
    for (std::size_t i = 1; i < segs.size(); ++i) {
      raw(level + 1, "CONT" + (segs[i].empty() ? std::string{} : " " + segs[i]));
    }
  }

  const std::string& str() const noexcept { return out_; }

 private:
  std::string out_;
  bool allow_conc_ = true;  // false = 7.0 模式（无 CONC）
};

// GenealogyDate → GEDCOM DATE 值。原文（lunar）优先，保证往返无损；
// 没有原文时才按精度合成。
std::string gedcom_date_value(const GenealogyDate& d) {
  if (d.lunar) return *d.lunar;
  if (!d.gregorian) return {};
  std::string prefix;
  if (d.qualifier) {
    if (*d.qualifier == "约") prefix = "ABT ";
    else if (*d.qualifier == "以前") prefix = "BEF ";
    else if (*d.qualifier == "以后") prefix = "AFT ";
  }
  const std::string& iso = *d.gregorian;
  // from_json 保证过格式，但导出边界不做假设：畸形 ISO 宁可原样
  // 带前缀输出，也不能在导出路径上抛异常炸掉整个文件。
  try {
    if (iso.size() == 10) {
      const int y = std::stoi(iso.substr(0, 4));
      const int m = std::stoi(iso.substr(5, 2));
      const int day = std::stoi(iso.substr(8, 2));
      if (m >= 1 && m <= 12) {
        return prefix + std::to_string(day) + " " + kMonthAbbr[m - 1] + " " +
               std::to_string(y);
      }
    } else if (iso.size() == 7) {
      const int y = std::stoi(iso.substr(0, 4));
      const int m = std::stoi(iso.substr(5, 2));
      if (m >= 1 && m <= 12) {
        return prefix + std::string(kMonthAbbr[m - 1]) + " " + std::to_string(y);
      }
    }
  } catch (const std::exception&) {
    return prefix + iso;
  }
  return prefix + iso;
}

const char* event_tag_of(EventType t) {
  switch (t) {
    case EventType::Birth: return "BIRT";
    case EventType::Death: return "DEAT";
    case EventType::Burial: return "BURI";
    case EventType::Marriage: return "MARR";
    case EventType::Migration: return "EMIG";
    case EventType::Examination: return "EDUC";
    case EventType::Office: return "OCCU";
    case EventType::Honor: return "TITL";
    case EventType::Residence: return "RESI";
    case EventType::Annotation: return "EVEN";
  }
  return "EVEN";
}

// unordered_map 取值：无则返回空 vector 的指针，避免调用方写两次查找
const std::vector<const Event*>* lookup_ptr(
    const std::unordered_map<std::string, std::vector<const Event*>>& m,
    const std::string& key) {
  auto it = m.find(key);
  return it != m.end() ? &it->second : nullptr;
}

const std::vector<std::string>& lookup_vec(
    const std::unordered_map<std::string, std::vector<std::string>>& m,
    const std::string& key) {
  static const std::vector<std::string> kEmpty;
  auto it = m.find(key);
  return it != m.end() ? it->second : kEmpty;
}

}  // namespace

std::string export_gedcom(const ExportOptions& opt) {
  // M9.3：目标版本分派。7.0 与 5.5.1 并存（自动识别导入已天然支持——
  // 语法层版本无关，映射层宽松读）。
  // 空串 = 未设置（位置式聚合初始化的 {}），按缺省 5.5.1 处理；
  // 显式传了未知版本才拒绝——不写出自称未支持版本的文件
  const std::string ver =
      opt.gedcom_version.empty() ? std::string("5.5.1") : opt.gedcom_version;
  if (ver != "5.5.1" && ver != "7.0") {
    return std::string();  // 空输出 = 调用方报告「不支持的版本」
  }
  const bool is_70 = ver == "7.0";
  LineWriter w(!is_70);
  // 导出侧损失台账。部分导出时缺集引用、无法表达的亲子链都在这里
  // 逐条记账——导出与导入同样不许静默丢（高阳李氏试修实证）。
  std::vector<MappingLoss> fallback_losses;
  std::vector<MappingLoss>* losses =
      opt.losses != nullptr ? opt.losses : &fallback_losses;
  auto export_loss = [&](LossSeverity sev, const std::string& xref,
                         const std::string& tag, const std::string& reason) {
    losses->push_back(MappingLoss{sev, xref, tag, reason});
  };
  std::unordered_set<std::string> person_ids;
  if (opt.persons) {
    for (const auto& p : *opt.persons) person_ids.insert(p.id);
  }
  std::unordered_set<std::string> union_ids;
  if (opt.unions) {
    for (const auto& u : *opt.unions) union_ids.insert(u.id);
  }
  std::unordered_map<std::string, std::string> place_names;
  if (opt.places) {
    for (const auto& p : *opt.places) place_names.emplace(p.id, p.name);
  }
  std::unordered_map<std::string, const Place*> place_index;
  if (opt.places) {
    for (const auto& pl : *opt.places) place_index.emplace(pl.id, &pl);
  }
  auto place_name = [&](const std::optional<std::string>& id) -> std::string {
    if (!id) return {};
    auto it = place_index.find(*id);
    if (it == place_index.end()) {
      // 地点不在导出集内：PLAC 缺失，地点引用断链——记账
      export_loss(LossSeverity::Warning, "", "PLAC",
                  "地点 " + *id + " 不在导出集内，地名无法表达");
      return std::string{};
    }
    return it->second->name;
  };
  // PLAC 行 + 可选 MAP 坐标（第五轮 P1-4：导入建档的经纬度此前导出
  // 不写，往返必丢；GEDCOM 5.5.1 的 PLAC.MAP.LATI/LONG 正是承载位）。
  auto write_plac = [&](int level, const std::optional<std::string>& id) {
    const std::string n = place_name(id);
    if (n.empty()) return;
    w.text(level, "PLAC", n);
    if (!id) return;
    auto it = place_index.find(*id);
    if (it == place_index.end()) return;
    const Place& pl = *it->second;
    if (pl.lat && pl.lng) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.6f", *pl.lat);
      w.raw(level + 1, "MAP");
      w.text(level + 2, "LATI", std::string(buf));
      std::snprintf(buf, sizeof(buf), "%.6f", *pl.lng);
      w.text(level + 2, "LONG", std::string(buf));
    }
  };

  w.raw(0, "HEAD");
  w.raw(1, "SOUR genealogy");
  w.raw(2, "NAME genealogy");
  if (!opt.source_version.empty()) w.raw(2, "VERS " + opt.source_version);
  w.raw(2, "CORP 本地家谱库");
  w.raw(1, "DEST GEDCOM");
  w.raw(1, "DATE " + iso_to_gedcom_datetime(utc_now_iso()));
  w.raw(1, "SUBM @U1@");
  w.raw(1, "GEDC");
  w.raw(2, "VERS " + (is_70 ? std::string("7.0") : std::string("5.5.1")));
  w.raw(2, "FORM LINEAGE-LINKED");
  if (is_70) {
    // 7.0 必填：HEAD.SPEC 声明规范 URI；自家 _CONC 扩展一并在方言
    // 声明里注明（7.0 弃用 _ 前缀惯例，允许任意标签但须 SPEC 声明）
    w.raw(2, "SPEC https://gedcom.io/familysearch-gedcom/v7.html");
    w.raw(2,
          "NOTE 本文件含自家方言标签 _CONC（妾/继配成员，payload 为人物 xref）");
  }
  w.raw(1, "CHAR UTF-8");
  w.raw(0, "@U1@ SUBM");
  w.text(1, "NAME", opt.submitter_name.empty() ? std::string("genealogy")
                                               : opt.submitter_name);

  // xref 分配：@I1@ / @F1@，按给定顺序，保证同一次导出内自洽
  std::unordered_map<std::string, std::string> person_xref;
  std::unordered_map<std::string, std::string> union_xref;
  int indi_no = 0;
  if (opt.persons) {
    for (const auto& p : *opt.persons) {
      person_xref.emplace(p.id, "@I" + std::to_string(++indi_no) + "@");
    }
  }
  int fam_no = 0;
  if (opt.unions) {
    for (const auto& u : *opt.unions) {
      union_xref.emplace(u.id, "@F" + std::to_string(++fam_no) + "@");
    }
  }
  // M7.5：来源 xref（@S1@…）。来源不属于任何导出过滤维度（表无
  // lineage 列），给全量分配 xref；没被任何 citation 引用的来源
  // 照写 0 SOUR 记录——出处档案跟着谱走。
  std::unordered_map<std::string, std::string> source_xref;
  int source_no = 0;
  if (opt.sources) {
    for (const auto& s : *opt.sources) {
      source_xref.emplace(s.id, "@S" + std::to_string(++source_no) + "@");
    }
  }

  // 事件按主体归拢：必须写进对应记录内部，不能挂在最后一条下面
  std::unordered_map<std::string, std::vector<const Event*>> events_of_person;
  std::unordered_map<std::string, std::vector<const Event*>> events_of_union;
  std::unordered_set<std::string> event_ids;
  if (opt.events) {
    for (const auto& e : *opt.events) {
      // 主体不在导出集内的事件：GEDCOM 里没有挂靠点，静默丢等于
      // 谱师看不到这条事实——记账
      const std::string where =
          e.subject_type == EventSubjectType::Person
              ? (person_ids.count(e.subject_id) > 0 ? "in" : "person-missing")
              : (union_ids.count(e.subject_id) > 0 ? "in" : "union-missing");
      if (where == "person-missing" || where == "union-missing") {
        export_loss(LossSeverity::Warning, "", "EVENT",
                    "事件（" + std::string(to_string(e.type)) + "）的主体 " +
                        e.subject_id + " 不在导出集内，事件无法挂靠");
        continue;
      }
      event_ids.insert(e.id);
      if (e.subject_type == EventSubjectType::Person) {
        events_of_person[e.subject_id].push_back(&e);
      } else if (e.subject_type == EventSubjectType::Union) {
        events_of_union[e.subject_id].push_back(&e);
      }
    }
  }

  // M7.5：citation 按主体归拢——写 INDI/FAM/事实行时取用。来源不在
  // 导出集（不该发生：sources 全量分配了 xref）或主体不在导出集的
  // 引用逐条记账；migration/branch/lineage 主体不在 GEDCOM 导出范围。
  std::unordered_map<std::string, std::vector<const Citation*>>
      person_cites;
  std::unordered_map<std::string, std::vector<const Citation*>> union_cites;
  std::unordered_map<std::string, std::vector<const Citation*>> event_cites;
  if (opt.citations && opt.sources) {
    for (const auto& c : *opt.citations) {
      if (source_xref.count(c.source_id) == 0) {
        export_loss(LossSeverity::Warning, "", "SOUR",
                    "引用的来源 " + c.source_id + " 不在来源表中，出处链缺失");
        continue;
      }
      if (c.subject_type == CitationSubjectType::Person) {
        if (person_ids.count(c.subject_id) > 0) {
          person_cites[c.subject_id].push_back(&c);
        } else {
          export_loss(LossSeverity::Warning, "", "SOUR",
                      "人物 " + c.subject_id + " 不在导出集内，其证据引用无法挂靠");
        }
      } else if (c.subject_type == CitationSubjectType::Union) {
        if (union_ids.count(c.subject_id) > 0) {
          union_cites[c.subject_id].push_back(&c);
        } else {
          export_loss(LossSeverity::Warning, "", "SOUR",
                      "结合 " + c.subject_id + " 不在导出集内，其证据引用无法挂靠");
        }
      } else if (c.subject_type == CitationSubjectType::Event) {
        if (event_ids.count(c.subject_id) > 0) {
          event_cites[c.subject_id].push_back(&c);
        } else {
          export_loss(LossSeverity::Warning, "", "SOUR",
                      "事件 " + c.subject_id + " 不在导出集内，其证据引用无法挂靠");
        }
      } else {
        export_loss(LossSeverity::Warning, "", "SOUR",
                    "主体类型 " + std::string(to_string(c.subject_type)) +
                        " 不在 GEDCOM 导出范围，证据引用无法表达");
      }
    }
  }

  // 配偶/父母反向索引：FAMS 来自 union_member，FAMC 与 FAM.CHIL 由父母
  // id 反查。
  //
  // 反查一律走哈希表：写成「每个结合扫一遍全部人物」是 O(结合×人物)，
  // 10 万人物 2.5 万结合就是 2.5e9 次比较，导出直接卡死（实测 1 万人物
  // 就要 57 秒，其中大半花在这种扫描上）。
  std::unordered_map<std::string, std::vector<std::string>> fams_of_person;
  std::unordered_map<std::string, std::vector<std::string>> famc_of_person;
  // 结合 id → (father_id, mother_id)，写 FAM.CHIL 时用
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      parents_of_union;
  // 结合 id → 该结合的子女（人物指针）。GEDCOM 的权威亲子关系在 FAM.CHIL
  std::unordered_map<std::string, std::vector<const Person*>> children_of_union;
  // (father_id, mother_id) → 结合 id 列表。人物找自己那个家庭走这张表。
  // 键用带分隔符的字符串而不是 pair：标准库没有 pair 的哈希。
  // 分隔符取 \x1F（ASCII 单元分隔符），不会出现在 ULID 里。
  auto parent_key = [](const std::string& f, const std::string& m) {
    return f + "\x1F" + m;
  };
  std::unordered_map<std::string, std::vector<std::string>> union_of_parents;
  // 单亲合成 FAM 的分组（仅父/仅母 → 子女列表）
  std::unordered_map<std::string, std::vector<const Person*>>
      solo_children_by_father;
  std::unordered_map<std::string, std::vector<const Person*>>
      solo_children_by_mother;

  if (opt.unions && opt.persons) {
    std::unordered_map<std::string, const Person*> by_id;
    by_id.reserve(opt.persons->size() * 2);
    for (const auto& p : *opt.persons) by_id.emplace(p.id, &p);

    // 第一遍：结合 → 配偶与父母 id
    for (const auto& u : *opt.unions) {
      for (const auto& m : u.members) {
        if (person_xref.count(m.person_id) > 0) {
          fams_of_person[m.person_id].push_back(union_xref[u.id]);
        }
      }
      std::string father_id;
      std::string mother_id;
      for (const auto& m : u.members) {
        const auto it = by_id.find(m.person_id);
        if (it == by_id.end()) continue;
        if (m.role == MemberRole::Husband) father_id = it->second->id;
        if (m.role == MemberRole::Wife) mother_id = it->second->id;
      }
      parents_of_union.emplace(u.id, std::make_pair(father_id, mother_id));
      // 亲子挂靠依据：夫妻齐全的结合照旧；妾室结合（无正室）时子女的
      // 母位由妾成员顶上——数据里 mother_id 明确指向妾本人，不存在
      // 「该挂哪一房」的谱学判断（那是正室与妾并存的混合 FAM 才有的
      // 问题，本模型里妾是独立结合）。此前一律不挂，妾出子女的父母链
      // 在往返中静默丢失（高阳李氏试修实证：东涵母夏氏往返归零）。
      if (!father_id.empty() && !mother_id.empty()) {
        union_of_parents[parent_key(father_id, mother_id)].push_back(u.id);
        children_of_union.emplace(u.id, std::vector<const Person*>{});
      } else if (u.type == UnionType::Concubinage && !father_id.empty()) {
        for (const auto& m : u.members) {
          if (m.role == MemberRole::Concubine) {
            union_of_parents[parent_key(father_id, m.person_id)].push_back(u.id);
            children_of_union.emplace(u.id, std::vector<const Person*>{});
            break;
          }
        }
      }
    }

    // 第二遍：人物 → 所属结合（FAMC）。父+母成对时挂对应结合；
    // 妾室结合按上面的母位顶替规则命中。
    for (const auto& p : *opt.persons) {
      if (!p.father_id || !p.mother_id) continue;
      const auto it =
          union_of_parents.find(parent_key(*p.father_id, *p.mother_id));
      if (it == union_of_parents.end()) continue;
      for (const auto& uid : it->second) {
        famc_of_person[p.id].push_back(union_xref.at(uid));
        auto kids = children_of_union.find(uid);
        if (kids != children_of_union.end()) kids->second.push_back(&p);
      }
    }

    // 单亲（仅父或仅母）且无任何结合可挂的子女：合成交子/交母专用
    // FAM。GEDCOM 的 FAM 本义是家庭单位而非婚姻——HUSB+CHIL 的单亲
    // 家庭合法（第三方文件里常见）。不合成的话父母链在往返中静默
    // 丢失（高阳李氏试修实证：国橑仅父知先，往返后父链归零）。
    for (const auto& p : *opt.persons) {
      if (famc_of_person.count(p.id) > 0) continue;
      if (p.father_id && !p.mother_id) {
        if (person_xref.count(*p.father_id) > 0) {
          solo_children_by_father[*p.father_id].push_back(&p);
        } else {
          export_loss(LossSeverity::Warning, person_xref.at(p.id), "FAMC",
                      "父亲 " + *p.father_id + " 不在导出集内，" +
                          p.family_name + p.given_name + " 的父链无法表达");
        }
      } else if (p.mother_id && !p.father_id) {
        if (person_xref.count(*p.mother_id) > 0) {
          solo_children_by_mother[*p.mother_id].push_back(&p);
        } else {
          export_loss(LossSeverity::Warning, person_xref.at(p.id), "FAMC",
                      "母亲 " + *p.mother_id + " 不在导出集内，" +
                          p.family_name + p.given_name + " 的母链无法表达");
        }
      } else if (p.father_id && p.mother_id) {
        // 父母都有却没有可挂的结合：双方至少一方缺集
        const bool f_in = person_xref.count(*p.father_id) > 0;
        const bool m_in = person_xref.count(*p.mother_id) > 0;
        if (!f_in || !m_in) {
          export_loss(LossSeverity::Warning, person_xref.at(p.id), "FAMC",
                      "父母（" + *p.father_id + " / " + *p.mother_id +
                          "）不在导出集内，" + p.family_name + p.given_name +
                          " 的亲子链无法表达");
        }
      }
    }
  }

  // M7.5：写出一条 citation（SOUR 行 + PAGE/QUAY/NOTE 子结构）。
  // QUAY 反向映射：high→3、medium→2、low→1、disputed→0（与导入半边
  // 对称，往返零损失）。
  auto confidence_to_quay = [](Confidence cf) -> const char* {
    switch (cf) {
      case Confidence::High: return "3";
      case Confidence::Medium: return "2";
      case Confidence::Low: return "1";
      case Confidence::Disputed: return "0";
    }
    return "2";
  };
  auto write_cite = [&](int level, const Citation& c) {
    const auto sit = source_xref.find(c.source_id);
    if (sit == source_xref.end()) return;  // 索引阶段已记账
    w.raw(level, "SOUR " + sit->second);
    if (c.locator && !c.locator->empty()) w.text(level + 1, "PAGE", *c.locator);
    w.raw(level + 1, std::string("QUAY ") + confidence_to_quay(c.confidence));
    if (c.note && !c.note->empty()) w.text(level + 1, "NOTE", *c.note);
  };

  // 某主体指定类型事件的 citation 写在 level（手写事实行下）。
  // Birth/Death/Marriage 的事件行由人物/结合字段手写（emit_facts 跳过），
  // 事件级 citation 必须跟手写行走，否则往返丢失（M7.5 实测踩过）。
  auto write_event_cites =
      [&](int level,
          const std::unordered_map<std::string, std::vector<const Event*>>&
              idx,
          const std::string& subject_id, EventType type) {
        const auto found = idx.find(subject_id);
        if (found == idx.end()) return;
        for (const Event* e : found->second) {
          if (e->type != type) continue;
          const auto it = event_cites.find(e->id);
          if (it != event_cites.end()) {
            for (const Citation* c : it->second) write_cite(level, *c);
          }
        }
      };

  // mask/year：M8.3 脱敏出口（第五轮 P2-1）——此前 emit_facts 完全
  // 不受 respect_privacy 约束，在世 Private 的住址/职业事件（含
  // TYPE 描述里的 PII）明文外泄。mask 抹日期/地点/描述；year 缩年份。
  auto emit_facts = [&](int level, const std::vector<const Event*>* evs,
                        std::initializer_list<EventType> skips,
                        bool mask_all = false, bool year_only_f = false) {
    if (evs == nullptr) return;
    for (const auto* e : *evs) {
      bool skip = false;
      for (const EventType t : skips) {
        if (e->type == t) { skip = true; break; }
      }
      if (skip) continue;
      w.raw(level, event_tag_of(e->type));
      if (!mask_all && e->description && !e->description->empty()) {
        w.text(level + 1, "TYPE", *e->description);
      }
      if (!mask_all && e->date &&
          (e->date->gregorian || e->date->lunar)) {
        std::string dv = gedcom_date_value(*e->date);
        if (year_only_f) {
          const std::string g =
              e->date->gregorian.value_or(std::string{});
          dv = g.size() >= 4 ? g.substr(0, 4) : std::string{};
        }
        if (!dv.empty()) w.text(level + 1, "DATE", dv);
      }
      if (!mask_all) write_plac(level + 1, e->place_id);
      if (!mask_all && e->description && !e->description->empty() &&
          e->type == EventType::Annotation) {
        w.text(level + 1, "NOTE", *e->description);
      }
      // 事件级 citation → 事实行下的 2 SOUR（M7.5）
      if (const auto it = event_cites.find(e->id); it != event_cites.end()) {
        for (const Citation* c : it->second) write_cite(level + 1, *c);
      }
    }
  };

  // 0 SOUR 来源记录（M7.5）：写在 INDI 之前（记录顺序对 GEDCOM 语义
  // 无影响，此处取「来源定义先于引用」的阅读习惯）。PUBB 为非标 tag，
  // 与导入侧对称——自家读取器认它，第三方软件会忽略，不会读错。
  if (opt.sources) {
    for (const auto& s : *opt.sources) {
      w.raw(0, source_xref[s.id] + " SOUR");
      w.text(1, "TITL", s.title);
      if (s.author) w.text(1, "AUTH", *s.author);
      if (s.repository) w.text(1, "PUBL", *s.repository);
      if (s.year_text) w.text(1, "PUBB", *s.year_text);
      if (s.citation_text) w.text(1, "TEXT", *s.citation_text);
      if (s.note) w.text(1, "NOTE", *s.note);
      if (s.uri && !s.uri->empty()) {
        export_loss(LossSeverity::Info, "", "SOUR",
                    "来源 " + s.title + " 的 uri 无 GEDCOM 对应表达，未导出");
      }
    }
  }
  // 地点补充字段台账（第五轮 P1-4）：经纬度已随 PLAC.MAP 输出，
  // 现代政区/沿革别名无 GEDCOM 槽位。
  if (opt.places) {
    for (const auto& pl : *opt.places) {
      if ((pl.modern_admin && !pl.modern_admin->empty()) ||
          (pl.historical_name && !pl.historical_name->empty())) {
        export_loss(LossSeverity::Info, "", "PLAC",
                    "地点 " + pl.name +
                        " 的现代政区/沿革别名无 GEDCOM 对应表达，未导出");
      }
    }
  }

  if (opt.persons) {
    for (const auto& p : *opt.persons) {
      w.raw(0, person_xref[p.id] + " INDI");
      const bool alias_has_slash =
          p.alias && p.alias->find('/') != std::string::npos;
      if (!p.family_name.empty() || !p.given_name.empty()) {
        w.text(1, "NAME",
               trim(p.given_name + " /" + p.family_name + "/" +
                    (p.alias && !alias_has_slash ? " " + *p.alias
                                                 : std::string{})));
        if (!p.given_name.empty()) w.text(2, "GIVN", p.given_name);
        if (!p.family_name.empty()) w.text(2, "SURN", p.family_name);
        if (p.alias && !p.alias->empty()) w.text(2, "NSFX", *p.alias);  // NSFX 全量保留,含含斜杠别名
      } else {
        w.text(1, "NAME", "（姓名缺失）");
      }
      w.raw(1, std::string("SEX ") +
                   (p.gender == Gender::Male
                        ? "M"
                        : (p.gender == Gender::Female ? "F" : "U")));
      // M8.3：在世者脱敏（respect_privacy=true 时）。private 抹日期、
      // kin 缩年份——gedcom_date_value 输出完整值，年份串在调用侧裁。
      const bool mask_dates =
          opt.respect_privacy && p.is_living &&
          p.privacy != PrivacyLevel::Public;
      const bool year_only =
          opt.respect_privacy && p.is_living &&
          p.privacy == PrivacyLevel::Kin;
      auto masked_date = [&](const GenealogyDate& d) -> std::string {
        const std::string dv = gedcom_date_value(d);
        if (dv.empty() || !year_only) return dv;
        // 取公历年份前缀（"1901-05-04" → "1901"）。纯农历无年份可给
        // ——不给日期（第五轮 P2-2）：完整农历原文会把 kin 级脱敏整个
        // 绕过（"同治三年四月初四" 明文外泄），谱书/版式管线同口径。
        const std::string g = d.gregorian.value_or(std::string{});
        return g.size() >= 4 ? g.substr(0, 4) : std::string{};
      };
      const bool has_birth =
          p.birth_date.gregorian.has_value() || p.birth_date.lunar.has_value();
      if (has_birth) {
        w.raw(1, "BIRT");
        const std::string dv = masked_date(p.birth_date);
        if (!dv.empty() && !mask_dates) w.text(2, "DATE", dv);
        else if (year_only && !dv.empty()) w.text(2, "DATE", dv);
        if (!mask_dates) {
          write_plac(2, p.birth_place_id);
        }
        write_event_cites(2, events_of_person, p.id, EventType::Birth);
      }
      const bool has_death = p.death_date.has_value() &&
                             (p.death_date->gregorian.has_value() ||
                              p.death_date->lunar.has_value());
      if (has_death) {
        w.raw(1, "DEAT");
        const std::string dv = masked_date(*p.death_date);
        if (!dv.empty() && !mask_dates) w.text(2, "DATE", dv);
        else if (year_only && !dv.empty()) w.text(2, "DATE", dv);
        if (!mask_dates) {
          write_plac(2, p.death_place_id);
        }
        write_event_cites(2, events_of_person, p.id, EventType::Death);
      } else if (p.is_living) {
        // 在世：显式 DEAT N（EVENT_Y_OR_N，N = 未发生）。此前写成
        // 「在世者输出 DEAT Y」——标准语义 Y = 死亡事件已发生，
        // 第三方软件会把活人读成死人，生死颠倒。
        w.raw(1, "DEAT N");
      } else if (const std::string n = place_name(p.death_place_id); !n.empty()) {
        // 已故、日期未知但卒地已登记：地点本身是死亡事实存在的证据，
        // 写 DEAT + PLAC 保住它（空值 = Y，同「事件已发生」），否则
        // 导入时记录的地点在往返后无声蒸发。
        w.raw(1, "DEAT");
        w.text(2, "PLAC", n);
        write_event_cites(2, events_of_person, p.id, EventType::Death);
      }
      // is_living=false 且无死亡日期、无卒地的人不写 DEAT：模型里
      // 「未登记」与「已故而无任何信息」同为 (is_living=false, 空)，
      // 无从区分——宁可不标，也不能把大片未知误标为已故。
      // 导入侧仍认 DEAT Y（第三方文件），见 handle_fact。
      const std::string burial = place_name(p.burial_place_id);
      const Event* burial_event = nullptr;
      if (const auto* evs = lookup_ptr(events_of_person, p.id)) {
        for (const Event* e : *evs) {
          if (e->type == EventType::Burial) { burial_event = e; break; }
        }
      }
      bool burial_emitted = false;
      if ((!burial.empty() || burial_event != nullptr) && !mask_dates) {
        // 葬期来自 Burial 事件（第五轮 P2-4）：此前手写 BURI 只带 PLAC，
        // 事件里的葬期被 emit_facts 跳过后整体丢失，且两条 1 BURI 会被
        // 第三方校验器判非法。
        w.raw(1, "BURI");
        if (burial_event != nullptr && burial_event->date &&
            (burial_event->date->gregorian ||
             burial_event->date->lunar)) {
          const std::string dv = gedcom_date_value(*burial_event->date);
          if (!dv.empty()) w.text(2, "DATE", dv);
        }
        write_plac(2, p.burial_place_id);
        write_event_cites(2, events_of_person, p.id, EventType::Burial);
        burial_emitted = true;
      }
      // M7.5：人物 citation 路由——已知字段级 locator（birth_date/
      // death_date/burial）落到对应事实行下（2 SOUR），其余作为记录级
      // 1 SOUR。无事实行可落的 locator（如事实行未写出）降级为记录级
      // 并由 2 PAGE 保留原文，往返不丢。
      if (const auto it = person_cites.find(p.id); it != person_cites.end()) {
        for (const Citation* c : it->second) {
          const std::string loc = c->locator ? *c->locator : "";
          if (loc == "birth_date" && has_birth) {
            write_cite(2, *c);
          } else if (loc == "death_date" && has_death) {
            write_cite(2, *c);
          } else if (loc == "burial" && !burial.empty()) {
            write_cite(2, *c);
          } else {
            write_cite(1, *c);
          }
        }
      }
      // NOTE 与传记类备注同属「private 全脱」面（GEDCOM 导出会照写）
      const bool show_notes =
          !mask_dates || p.privacy == PrivacyLevel::Public;
      if (show_notes && p.note && !p.note->empty()) {
        w.text(1, "NOTE", *p.note);
      }
      // Burial 已并入手写 BURI 行（葬期），此处跳过避免双写
      std::initializer_list<EventType> indi_skips = burial_emitted
          ? std::initializer_list<EventType>{EventType::Birth,
                                             EventType::Death,
                                             EventType::Burial}
          : std::initializer_list<EventType>{EventType::Birth,
                                             EventType::Death};
      emit_facts(1, lookup_ptr(events_of_person, p.id), indi_skips,
                 mask_dates, year_only);
      for (const auto& f : lookup_vec(famc_of_person, p.id)) w.raw(1, "FAMC " + f);
      for (const auto& f : lookup_vec(fams_of_person, p.id)) w.raw(1, "FAMS " + f);
      if (!p.updated_at.empty()) {
        w.raw(1, "CHAN");
        w.text(2, "DATE", iso_to_gedcom_datetime(p.updated_at));
      }
      // 导出侧损失台账（第五轮 P1-4）：库里有、GEDCOM 无对应表达的
      // 人物字段逐条记账，不再无声消失。
      {
        std::string dropped;
        auto add_drop = [&](const char* what) {
          if (!dropped.empty()) dropped += "/";
          dropped += what;
        };
        if (p.courtesy_name && !p.courtesy_name->empty()) add_drop("字");
        if (p.art_name && !p.art_name->empty()) add_drop("号");
        if (p.posthumous_title && !p.posthumous_title->empty())
          add_drop("谥号");
        if (p.birth_order) add_drop("行第");
        if (p.generation_no) add_drop("世代序号");
        if (p.is_heir) add_drop("承重标记");
        if (p.branch_id && !p.branch_id->empty()) add_drop("支派归属");
        if (p.biography && !p.biography->empty()) add_drop("行传");
        if (p.portrait_attachment_id && !p.portrait_attachment_id->empty())
          add_drop("照片引用");
        if (!dropped.empty()) {
          export_loss(LossSeverity::Info, person_xref.at(p.id), "INDI",
                      dropped + " 等字段 GEDCOM 无对应表达，未导出");
        }
      }
    }
  }

  std::unordered_map<std::string, const Person*> person_index;
  if (opt.persons) {
    for (const auto& pp : *opt.persons) person_index.emplace(pp.id, &pp);
  }

  if (opt.unions) {
    for (const auto& u : *opt.unions) {
      w.raw(0, union_xref[u.id] + " FAM");
      for (const auto& m : u.members) {
        auto it = person_xref.find(m.person_id);
        if (it == person_xref.end()) {
          // 成员不在导出集内：FAM 少一个配偶，家庭结构受损——记账
          export_loss(LossSeverity::Warning, union_xref.at(u.id), m.role == MemberRole::Husband ? "HUSB" : (m.role == MemberRole::Wife ? "WIFE" : "_CONC"),
                      "结合成员 " + m.person_id + " 不在导出集内，配偶链缺失");
          continue;
        }
        // 妾/继配在 GEDCOM 5.5.1 无对应角色，用扩展标签 _CONC 保留
        // （自家读取器认它，其它软件会忽略，不会读错）
        const char* tag = m.role == MemberRole::Husband
                              ? "HUSB"
                              : (m.role == MemberRole::Wife ? "WIFE" : "_CONC");
        w.raw(1, std::string(tag) + " " + it->second);
      }
      // FAM 出口的脱敏（第五轮 P2-1）：成员中任一在世 Private → 抹
      // 婚期/地点/备注；任一 Kin → 缩年份。此前 FAM 出口完全裸奔。
      bool fam_mask = false;
      bool fam_year = false;
      for (const auto& m : u.members) {
        auto pit = person_index.find(m.person_id);
        if (pit == person_index.end()) continue;
        const Person& mp = *pit->second;
        if (!mp.is_living) continue;
        if (mp.privacy == PrivacyLevel::Private) fam_mask = true;
        else if (mp.privacy == PrivacyLevel::Kin) fam_year = true;
      }
      const bool has_marr =
          u.start_date.has_value() &&
          (u.start_date->gregorian.has_value() || u.start_date->lunar.has_value());
      // 婚姻事件兜底（第五轮 P2-5）：字段缺日期但事件有完整 DATE/PLAC
      // 时，此前事件被 emit_facts 跳过、字段又为空，婚姻整体静默蒸发。
      const Event* marr_event = nullptr;
      if (const auto* evs = lookup_ptr(events_of_union, u.id)) {
        for (const Event* e : *evs) {
          if (e->type == EventType::Marriage) { marr_event = e; break; }
        }
      }
      if (has_marr || (!fam_mask && marr_event != nullptr)) {
        w.raw(1, "MARR");
        if (!fam_mask) {
          const GenealogyDate* src_date =
              has_marr ? &*u.start_date
                       : (marr_event && marr_event->date ? &*marr_event->date
                                                         : nullptr);
          if (src_date) {
            std::string dv = gedcom_date_value(*src_date);
            if (fam_year) {
              const std::string g =
                  src_date->gregorian.value_or(std::string{});
              dv = g.size() >= 4 ? g.substr(0, 4) : std::string{};
            }
            if (!dv.empty()) w.text(2, "DATE", dv);
          }
          write_plac(2, u.place_id ? u.place_id
                                   : (marr_event ? marr_event->place_id
                                                 : std::nullopt));
        }
        write_event_cites(2, events_of_union, u.id, EventType::Marriage);
      }
      if (u.status == UnionStatus::Dissolved) {
        w.raw(1, "DIV");
        if (u.end_date && (u.end_date->gregorian || u.end_date->lunar)) {
          const std::string dv = gedcom_date_value(*u.end_date);
          if (!dv.empty()) w.text(2, "DATE", dv);
        }
      }
      if (!fam_mask && u.note && !u.note->empty())
        w.text(1, "NOTE", *u.note);
      // M7.5：Union 级 citation → FAM 下的 1 SOUR
      if (const auto it = union_cites.find(u.id); it != union_cites.end()) {
        for (const Citation* c : it->second) write_cite(1, *c);
      }
      // 子女链：GEDCOM 的权威亲子关系在 FAM.CHIL，只写孩子的 FAMC 是不够的。
      // 反查表在建索引时已算好，这里只按结合取自己那一份。
      const auto kids = children_of_union.find(u.id);
      if (kids != children_of_union.end()) {
        for (const auto* child : kids->second) {
          const auto it = person_xref.find(child->id);
          if (it != person_xref.end()) {
            w.raw(1, "CHIL " + it->second);
          } else {
            export_loss(LossSeverity::Warning, union_xref.at(u.id), "CHIL",
                        "子女 " + child->id + " 不在导出集内，亲子链缺失");
          }
        }
      }
      emit_facts(1, lookup_ptr(events_of_union, u.id),
                 {EventType::Marriage, EventType::Annotation}, fam_mask,
                 fam_year);
    }

    // 单亲合成 FAM：无婚姻语义，只有亲子女链。xref 用 SF 前缀与
    // 结合 FAM 区分；重导入只还原父/母链接、不造多余的结合。
    auto emit_solo_fam = [&](const std::string& parent_id,
                             const std::vector<const Person*>& kids,
                             const char* role) {
      const auto pit = person_xref.find(parent_id);
      if (pit == person_xref.end() || kids.empty()) return;
      w.raw(0, "@SF" + parent_id + "@ FAM");
      w.raw(1, std::string(role) + " " + pit->second);
      for (const auto* child : kids) {
        const auto it = person_xref.find(child->id);
        if (it != person_xref.end()) w.raw(1, "CHIL " + it->second);
      }
    };
    for (const auto& [father_id, kids] : solo_children_by_father) {
      emit_solo_fam(father_id, kids, "HUSB");
    }
    for (const auto& [mother_id, kids] : solo_children_by_mother) {
      emit_solo_fam(mother_id, kids, "WIFE");
    }
  }

  w.raw(0, "TRLR");
  return w.str();
}

}  // namespace genealogy::io
