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
  }
  return true;
}

std::size_t build_subtree(const std::vector<FlatLine>& f, std::size_t i,
                          GedcomNode* out) {
  out->level = f[i].level;
  out->xref = f[i].xref;
  out->tag = f[i].tag;
  out->value = f[i].value;
  std::size_t j = i + 1;
  // 跳级（level 不连续）按「上一级的子节点」容忍处理，不让整份文件报废
  while (j < f.size() && f[j].level > f[i].level) {
    GedcomNode child;
    j = build_subtree(f, j, &child);
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

Person* find_person(std::vector<Person>& persons, const std::string& id) {
  for (auto& p : persons) {
    if (p.id == id) return &p;
  }
  return nullptr;
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
};

}  // namespace

// ---------------------------------------------------------------- 映射层

std::vector<Person> order_by_dependency(const std::vector<Person>& all) {
  auto in_batch = [&all](const std::string& id) {
    for (const auto& p : all) {
      if (p.id == id) return true;
    }
    return false;
  };
  std::vector<Person> pending = all;
  std::vector<Person> ordered;
  std::unordered_set<std::string> created;
  while (!pending.empty()) {
    const std::size_t before = pending.size();
    for (auto it = pending.begin(); it != pending.end();) {
      const bool father_ok = !it->father_id || !in_batch(*it->father_id) ||
                             created.count(*it->father_id) > 0;
      const bool mother_ok = !it->mother_id || !in_batch(*it->mother_id) ||
                             created.count(*it->mother_id) > 0;
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
  if (opt.lineage_id.empty()) {
    return invalid_argument("导入必须指定 lineage_id（谱要落到哪个宗族）");
  }
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

  // ---- 第一遍：INDI ----
  for (const auto& rec : doc.records) {
    if (rec.tag != "INDI") continue;
    const std::string xref = "@" + rec.xref + "@";
    Person p;
    p.id = generate_ulid();
    p.lineage_id = opt.lineage_id;
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

    const std::string sex = upper_ascii(rec.child_value("SEX"));
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
      // GEDCOM 用 "1 DEAT Y" 表示此人仍在世，那不是一条死亡事实
      if (ev_type == EventType::Death &&
          upper_ascii(trim(node->value)) == "Y") {
        p.is_living = true;
        return;
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
      }
    };

    handle_fact("BIRT", EventType::Birth, "");
    handle_fact("DEAT", EventType::Death, "");
    handle_fact("BURI", EventType::Burial, "");
    for (const auto& m : kEventTags) {
      handle_fact(m.tag, m.type, m.desc);
    }

    // 备注
    std::vector<std::string> notes;
    for (const auto* n : rec.children_all("NOTE")) notes.push_back(n->value);
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
      if (!v.empty()) p.updated_at = v;
    }
    for (const auto* obje : rec.children_all("OBJE")) {
      (void)obje;
      add_loss(LossSeverity::Warning, xref, "OBJE",
               "多媒体附件未建模（M1 无附件库），链接已丢失");
    }
    if (const auto* sour = rec.child("SOUR")) {
      (void)sour;
      add_loss(LossSeverity::Warning, xref, "SOUR",
               "证据引用未建模（M1 无 Source/Citation），出处链已丢失");
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

    res.id_map.emplace(xref, p.id);
    res.persons.push_back(std::move(p));
  }

  // ---- 第二遍：FAM（此时所有人都已建档，可解引用） ----
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
    bool has_obje = false;
  };
  std::vector<FamRaw> fams;
  for (const auto& rec : doc.records) {
    if (rec.tag != "FAM") continue;
    FamRaw f;
    f.xref = "@" + rec.xref + "@";
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

    Union u;
    u.id = generate_ulid();
    u.rev = 1;
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
      Person* child = find_person(res.persons, it->second);
      if (child == nullptr) continue;
      if (!husb_id.empty() && !child->father_id) {
        child->father_id = husb_id;
        const auto* hp = find_person(res.persons, husb_id);
        if (hp && hp->gender == Gender::Unknown) {
          add_loss(LossSeverity::Info, f.xref, "CHIL",
                   "丈夫性别未知，按 GEDCOM HUSB 角色推定为父亲");
        }
      }
      if (!wife_id.empty() && !child->mother_id) {
        child->mother_id = wife_id;
        const auto* wp = find_person(res.persons, wife_id);
        if (wp && wp->gender == Gender::Unknown) {
          add_loss(LossSeverity::Info, f.xref, "CHIL",
                   "妻子性别未知，按 GEDCOM WIFE 角色推定为母亲");
        }
      }
    }

    if (f.marr) {
      if (const auto* d = f.marr->child("DATE")) {
        const DateOutcome o = parse_gedcom_date(d->value);
        u.start_date = o.date;
        if (!o.note.empty()) {
          add_loss(LossSeverity::Warning, f.xref, "MARR.DATE", o.note);
        }
        Event e;
        e.id = generate_ulid();
        e.subject_type = EventSubjectType::Union;
        e.subject_id = u.id;
        e.type = EventType::Marriage;
        e.date = o.date;
        e.created_at = now;
        if (const auto* pl = f.marr->child("PLAC")) {
          const std::string pid = place_for(trim(pl->value), pl, f.xref);
          if (!pid.empty()) e.place_id = pid;
        }
        res.events.push_back(e);
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
      u.status = UnionStatus::Dissolved;
      add_loss(LossSeverity::Info, f.xref, "DIV",
               "离异事件无专属类型，已记为结合状态 + 结束日期");
    }
    if (f.note) u.note = f.note->value;
    if (f.has_obje) {
      add_loss(LossSeverity::Warning, f.xref, "OBJE",
               "多媒体附件未建模（M1 无附件库），链接已丢失");
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
    // 「丢失的信息」；记 Info 只会淹没真正要人看的损失，故直接跳过
    if (rec.tag == "INDI" || rec.tag == "FAM" || rec.tag == "HEAD" ||
        rec.tag == "SUBM" || rec.tag == "SUBN" || rec.tag == "TRLR") {
      continue;
    }
    add_loss(LossSeverity::Info, "@" + rec.xref + "@", rec.tag,
             "记录类型 " + rec.tag + " 暂无对应模型，未导入");
  }
  return res;
}

// ---------------------------------------------------------------- 写出层
// 约束：单行不超过 255 字符（GEDCOM 规范），超长按 CONC 切、且必须在
// UTF-8 字符边界上切；文本里的换行走 CONT。这两条保证自家读取器
// 能无损读回（往返测试见 test_gedcom.cpp）。

namespace {

class LineWriter {
 public:
  void raw(int level, const std::string& body) {
    const std::string prefix = std::to_string(level) + " ";
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
      out_ += (first ? prefix : cont_prefix) + body.substr(start, end - start) +
              "\n";
      start = end;
      first = false;
    }
  }

  // 带可能含换行的文本值：首段作 value，其余段作 CONT
  void text(int level, const std::string& tag, const std::string& value) {
    std::vector<std::string> segs;
    std::string cur;
    for (const char c : value) {
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
  LineWriter w;
  std::unordered_map<std::string, std::string> place_names;
  if (opt.places) {
    for (const auto& p : *opt.places) place_names.emplace(p.id, p.name);
  }
  auto place_name = [&](const std::optional<std::string>& id) -> std::string {
    if (!id) return {};
    auto it = place_names.find(*id);
    return it != place_names.end() ? it->second : std::string{};
  };

  w.raw(0, "HEAD");
  w.raw(1, "SOUR genealogy");
  w.raw(2, "NAME genealogy");
  if (!opt.source_version.empty()) w.raw(2, "VERS " + opt.source_version);
  w.raw(2, "CORP 本地家谱库");
  w.raw(1, "DEST GEDCOM");
  w.raw(1, "DATE " + utc_now_iso());
  w.raw(1, "SUBM @U1@");
  w.raw(1, "GEDC");
  w.raw(2, "VERS 5.5.1");
  w.raw(2, "FORM LINEAGE-LINKED");
  w.raw(1, "CHAR UTF-8");
  w.raw(0, "@U1@ SUBM");
  w.raw(1, "NAME " + (opt.submitter_name.empty() ? std::string("genealogy")
                                                 : opt.submitter_name));

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

  // 事件按主体归拢：必须写进对应记录内部，不能挂在最后一条下面
  std::unordered_map<std::string, std::vector<const Event*>> events_of_person;
  std::unordered_map<std::string, std::vector<const Event*>> events_of_union;
  if (opt.events) {
    for (const auto& e : *opt.events) {
      if (e.subject_type == EventSubjectType::Person) {
        events_of_person[e.subject_id].push_back(&e);
      } else if (e.subject_type == EventSubjectType::Union) {
        events_of_union[e.subject_id].push_back(&e);
      }
    }
  }

  // 配偶/父母反向索引：FAMS 来自 union_member，FAMC 与 FAM.CHIL 由父母
  // id 反查。父母 id 先按结合存下来，导出 FAM 时还要用来写 CHIL。
  std::unordered_map<std::string, std::vector<std::string>> fams_of_person;
  std::unordered_map<std::string, std::vector<std::string>> famc_of_person;
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      parents_of_union;   // union id → (father_id, mother_id)
  if (opt.unions && opt.persons) {
    for (const auto& u : *opt.unions) {
      for (const auto& m : u.members) {
        if (person_xref.count(m.person_id)) {
          fams_of_person[m.person_id].push_back(union_xref[u.id]);
        }
      }
      const Person* husb = nullptr;
      const Person* wife = nullptr;
      for (const auto& p : *opt.persons) {
        for (const auto& m : u.members) {
          if (m.person_id == p.id) {
            if (m.role == MemberRole::Husband) husb = &p;
            if (m.role == MemberRole::Wife) wife = &p;
          }
        }
      }
      const std::string father_id = husb ? husb->id : std::string{};
      const std::string mother_id = wife ? wife->id : std::string{};
      parents_of_union.emplace(u.id, std::make_pair(father_id, mother_id));
      // 只认「夫妻双方齐全」的结合作为亲子关系依据：妾/继配所生的孩子
      // 该挂在哪一房是谱学判断，程序不替谱师决定，挂错比不挂更难发现
      if (father_id.empty() || mother_id.empty()) continue;
      for (const auto& p : *opt.persons) {
        // 只有非空的一侧参与匹配，否则空 id 会误抓一堆人
        const bool father_match = !father_id.empty() && p.father_id == father_id;
        const bool mother_match = !mother_id.empty() && p.mother_id == mother_id;
        if (father_match || mother_match) {
          famc_of_person[p.id].push_back(union_xref[u.id]);
        }
      }
    }
  }

  auto emit_facts = [&](int level, const std::vector<const Event*>* evs,
                        EventType skip_type_a, EventType skip_type_b) {
    if (evs == nullptr) return;
    for (const auto* e : *evs) {
      if (e->type == skip_type_a || e->type == skip_type_b) continue;
      w.raw(level, event_tag_of(e->type));
      if (e->description && !e->description->empty()) {
        w.raw(level + 1, "TYPE " + *e->description);
      }
      if (e->date && (e->date->gregorian || e->date->lunar)) {
        const std::string dv = gedcom_date_value(*e->date);
        if (!dv.empty()) w.text(level + 1, "DATE", dv);
      }
      const std::string n = place_name(e->place_id);
      if (!n.empty()) w.text(level + 1, "PLAC", n);
      if (e->description && !e->description->empty() &&
          e->type == EventType::Annotation) {
        w.text(level + 1, "NOTE", *e->description);
      }
    }
  };

  if (opt.persons) {
    for (const auto& p : *opt.persons) {
      w.raw(0, person_xref[p.id] + " INDI");
      if (!p.family_name.empty() || !p.given_name.empty()) {
        w.text(1, "NAME",
               trim(p.given_name + " /" + p.family_name + "/" +
                    (p.alias ? " " + *p.alias : std::string{})));
        if (!p.given_name.empty()) w.raw(2, "GIVN " + p.given_name);
        if (!p.family_name.empty()) w.raw(2, "SURN " + p.family_name);
        if (p.alias && !p.alias->empty()) w.raw(2, "NSFX " + *p.alias);
      } else {
        w.text(1, "NAME", "（姓名缺失）");
      }
      w.raw(1, std::string("SEX ") +
                   (p.gender == Gender::Male
                        ? "M"
                        : (p.gender == Gender::Female ? "F" : "U")));
      const bool has_birth =
          p.birth_date.gregorian.has_value() || p.birth_date.lunar.has_value();
      if (has_birth) {
        w.raw(1, "BIRT");
        const std::string dv = gedcom_date_value(p.birth_date);
        if (!dv.empty()) w.text(2, "DATE", dv);
        const std::string n = place_name(p.birth_place_id);
        if (!n.empty()) w.text(2, "PLAC", n);
      }
      const bool has_death = p.death_date.has_value() &&
                             (p.death_date->gregorian.has_value() ||
                              p.death_date->lunar.has_value());
      if (has_death) {
        w.raw(1, "DEAT");
        const std::string dv = gedcom_date_value(*p.death_date);
        if (!dv.empty()) w.text(2, "DATE", dv);
        const std::string n = place_name(p.death_place_id);
        if (!n.empty()) w.text(2, "PLAC", n);
      } else if (p.is_living) {
        w.raw(1, "DEAT Y");   // GEDCOM 约定：Y = 此人仍在世
      }
      const std::string burial = place_name(p.burial_place_id);
      if (!burial.empty()) {
        w.raw(1, "BURI");
        w.text(2, "PLAC", burial);
      }
      if (p.note && !p.note->empty()) w.text(1, "NOTE", *p.note);
      emit_facts(1, lookup_ptr(events_of_person, p.id), EventType::Birth,
                 EventType::Death);
      for (const auto& f : lookup_vec(famc_of_person, p.id)) w.raw(1, "FAMC " + f);
      for (const auto& f : lookup_vec(fams_of_person, p.id)) w.raw(1, "FAMS " + f);
      if (!p.updated_at.empty()) {
        w.raw(1, "CHAN");
        w.raw(2, "DATE " + p.updated_at);
      }
    }
  }

  if (opt.unions) {
    for (const auto& u : *opt.unions) {
      w.raw(0, union_xref[u.id] + " FAM");
      for (const auto& m : u.members) {
        auto it = person_xref.find(m.person_id);
        if (it == person_xref.end()) continue;   // 成员不在本次导出集内
        // 妾/继配在 GEDCOM 5.5.1 无对应角色，用扩展标签 _CONC 保留
        // （自家读取器认它，其它软件会忽略，不会读错）
        const char* tag = m.role == MemberRole::Husband
                              ? "HUSB"
                              : (m.role == MemberRole::Wife ? "WIFE" : "_CONC");
        w.raw(1, std::string(tag) + " " + it->second);
      }
      const bool has_marr =
          u.start_date.has_value() &&
          (u.start_date->gregorian.has_value() || u.start_date->lunar.has_value());
      if (has_marr) {
        w.raw(1, "MARR");
        const std::string dv = gedcom_date_value(*u.start_date);
        if (!dv.empty()) w.text(2, "DATE", dv);
        const std::string n = place_name(u.place_id);
        if (!n.empty()) w.text(2, "PLAC", n);
      }
      if (u.status == UnionStatus::Dissolved) {
        w.raw(1, "DIV");
        if (u.end_date && (u.end_date->gregorian || u.end_date->lunar)) {
          const std::string dv = gedcom_date_value(*u.end_date);
          if (!dv.empty()) w.text(2, "DATE", dv);
        }
      }
      if (u.note && !u.note->empty()) w.text(1, "NOTE", *u.note);
      // 子女链：GEDCOM 的权威亲子关系在 FAM.CHIL，只写孩子的 FAMC 是不够的
      if (opt.persons) {
        const auto par = parents_of_union.find(u.id);
        const std::string father_id =
            par == parents_of_union.end() ? std::string{} : par->second.first;
        const std::string mother_id =
            par == parents_of_union.end() ? std::string{} : par->second.second;
        for (const auto& c : *opt.persons) {
          const bool father_match =
              !father_id.empty() && c.father_id == father_id;
          const bool mother_match =
              !mother_id.empty() && c.mother_id == mother_id;
          if (!father_match && !mother_match) continue;
          auto it = person_xref.find(c.id);
          if (it != person_xref.end()) w.raw(1, "CHIL " + it->second);
        }
      }
      emit_facts(1, lookup_ptr(events_of_union, u.id), EventType::Marriage,
                 EventType::Annotation);
    }
  }

  w.raw(0, "TRLR");
  return w.str();
}

}  // namespace genealogy::io
