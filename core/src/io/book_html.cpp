#include <genealogy/io/book_html.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace genealogy::io {

namespace {

const char* kSerifFont = "SourceHanSerifSC-Regular.otf";

std::string html_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

// CSS 单引号字符串上下文专用转义（html_escape 只处理 HTML 正文，
// 不处理 ' 与 \，书名注入到 @page content:'...' 时需要）。
std::string css_string_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '\\' || c == '\'') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c == '<' || c == '>' || c == '&') {
      // <style> 是 raw text 元素，HTML 实体不会被解码——必须用 CSS
      // 十六进制转义（\3C 后跟空格结束转义序列），否则页眉字面显示 &lt;。
      static const char hex[] = "0123456789abcdef";
      out += "\\";
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
      out += ' ';
    } else if (c == '\n' || c == '\r') {
      out += "\\A ";  // CSS 换行转义
    } else if (c < 0x20) {
      // 控制字符用 CSS 十六进制转义
      static const char hex[] = "0123456789abcdef";
      out += "\\";
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
      out += ' ';
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

std::string read_binary(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return {};
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

static const char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const std::string& in) {
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < in.size(); i += 3) {
    const unsigned char a = static_cast<unsigned char>(in[i]);
    const unsigned char b = i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0;
    const unsigned char c = i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
    out += kBase64[a >> 2];
    out += kBase64[((a & 3) << 4) | (b >> 4)];
    out += i + 1 < in.size() ? kBase64[((b & 15) << 2) | (c >> 6)] : '=';
    out += i + 2 < in.size() ? kBase64[c & 63] : '=';
  }
  return out;
}

int generation_of(const Person& p) { return p.generation_no.value_or(0); }

std::string life_span(const Person& p) {
  std::string s;
  if (p.birth_date.gregorian) {
    s = *p.birth_date.gregorian;
  } else if (p.birth_date.lunar) {
    s = *p.birth_date.lunar;
  }
  if (p.death_date) {
    const auto& d = *p.death_date;
    if (d.gregorian || d.lunar) s += "—" + (d.gregorian ? *d.gregorian : *d.lunar);
  }
  return s;
}

// 分组结果连同越界说明一起传出，避免全局可变状态（并发安全）。
struct GenerationGroups {
  std::vector<std::vector<const Person*>> groups;
  std::string invalid_note;
};

// 按世代分组，族内按出生序/ID 稳定排序。
GenerationGroups by_generation(const std::vector<Person>& persons) {
  std::vector<const Person*> sorted;
  sorted.reserve(persons.size());
  for (const auto& p : persons) sorted.push_back(&p);
  std::stable_sort(sorted.begin(), sorted.end(), [](const Person* a, const Person* b) {
    const int ga = generation_of(*a), gb = generation_of(*b);
    if (ga != gb) return ga < gb;
    const int ba = a->birth_order.value_or(0), bb = b->birth_order.value_or(0);
    if (ba != bb) return ba < bb;
    return a->id < b->id;
  });
  std::vector<std::vector<const Person*>> groups;
  std::string note;
  for (const Person* p : sorted) {
    const int g = generation_of(*p);
    // 负世代/超大世代序号不可作为数组下标：前者转 size_t 会翻转成巨大值
    // （实测 vector::resize 抛异常），后者 g+2 有符号溢出。越界者归入
    // 单独的"世代异常"组，保证导出不崩且数据可见。
    constexpr int kMaxGeneration = 10000;
    if (g < 0 || g > kMaxGeneration) {
      if (note.empty())
        note = "世代序号越界人物: ";
      else note += ", ";
      note += p->family_name + p->given_name + "(" +
                             std::to_string(g) + ")";
      continue;
    }
    const std::size_t gi = static_cast<std::size_t>(g) + 1;
    if (groups.size() <= gi) groups.resize(gi + 1);
    groups[gi].push_back(p);
  }
  return {std::move(groups), std::move(note)};
}

}  // namespace

const char* to_string(BookTemplate t) {
  switch (t) {
    case BookTemplate::Register: return "register";
    case BookTemplate::Genealogy: return "genealogy";
    case BookTemplate::Biography: return "biography";
  }
  return "register";
}

const char* to_string(PaperSize p) {
  return p == PaperSize::A4 ? "a4" : "16k";
}

Result<std::string> render_book_html(const BookHtmlOptions& opt) {
  std::string font_face;
  if (opt.embed_fonts && !opt.font_dir.empty()) {
    // font_dir 来自命令行，禁止 ':' 防 CSS url 注入（路径本身已由调用方给）
    const std::string path = opt.font_dir + "/" + kSerifFont;
    const std::string data = read_binary(path);
    if (data.empty()) {
      return Error{ErrorCode::IoError, "出版字体读取失败: " + path};
    }
    font_face = "@font-face{font-family:'GeneaSerif';src:url(data:font/otf;base64," +
                base64(data) + ") format('opentype');font-weight:400;font-style:normal;}";
  } else {
    font_face = "@font-face{font-family:'GeneaSerif';src:local('Source Han Serif SC'),"
                "local('Songti SC'),local('SimSun');}";
  }

  const std::string title =
      (opt.book && !opt.book->name.empty()) ? opt.book->name : "家谱";
  const std::string surname = opt.book ? opt.book->surname : "";
  const std::string hall = (opt.book && opt.book->hall_name) ? *opt.book->hall_name : "";
  const std::string poem =
      (opt.book && opt.book->generation_poem) ? *opt.book->generation_poem : "";

  const std::string page_size = opt.paper == PaperSize::K16 ? "185mm 260mm" : "210mm 297mm";
  const std::string size_css = opt.landscape ? "landscape" : "portrait";

  std::ostringstream h;
  h << "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>\n"
    << "<title>" << html_escape(title) << "</title>\n<style>\n"
    << font_face << "\n"
    << "@page{size:" << page_size << ";" << size_css
    << ";margin:18mm 16mm 18mm 22mm;"
    << "@top-center{content:'" << css_string_escape(title) << "';}"
    << "@bottom-center{content:counter(page);}}\n"
    << "body{font-family:'GeneaSerif',serif;font-size:11pt;line-height:1.75;"
    << "color:#111;counter-reset:page;}\n"
    << ".cover{page-break-after:always;text-align:center;padding-top:60mm;"
    << "writing-mode:vertical-rl;height:140mm;}\n"
    << ".cover h1{font-size:30pt;letter-spacing:6pt;margin:0;}\n"
    << ".cover .sub{font-size:13pt;color:#555;margin-left:12mm;}\n"
    << ".poem{page-break-after:always;writing-mode:vertical-rl;"
    << "height:220mm;letter-spacing:3pt;}\n"
    << ".poem h2{font-size:16pt;}\n"
    << "h2.gen{page-break-before:always;font-size:15pt;"
    << "border-bottom:1px solid #888;padding-bottom:3mm;}\n"
    << ".genealogy{writing-mode:vertical-rl;height:225mm;"
    << "column-count:1;}\n"
    << "table.register{border-collapse:collapse;width:100%;font-size:10pt;}\n"
    << "table.register th,table.register td{border:1px solid #999;padding:1.5mm 2mm;"
    << "vertical-align:top;}\n"
    << ".bio{page-break-inside:avoid;margin-bottom:5mm;}\n"
    << ".bio h3{margin:0 0 1mm;font-size:12pt;}\n"
    << ".note{color:#555;font-size:9pt;}\n"
    << "</style></head><body>\n";

  // 封面（竖排）
  h << "<div class='cover'><h1>" << html_escape(title) << "</h1>"
    << "<div class='sub'>" << html_escape(surname)
    << (hall.empty() ? "" : " · " + html_escape(hall)) << "</div></div>\n";

  // 派语（若有）
  if (!poem.empty()) {
    h << "<div class='poem'><h2>派语</h2><p>" << html_escape(poem) << "</p></div>\n";
  }

  // 注意：不能写 opt.persons ? *opt.persons : std::vector<Person>{} ——
  // 条件表达式会构造临时 vector，by_generation 的 const& 绑定后即悬垂。
  const std::vector<Person> empty_persons;
  const auto grouped = by_generation(opt.persons ? *opt.persons : empty_persons);
  const auto& groups = grouped.groups;
  switch (opt.tmpl) {
    case BookTemplate::Register:
      h << "<h2 class='gen'>世系表</h2><table class='register'>"
        << "<tr><th>世代</th><th>姓名</th><th>字/号</th><th>生卒</th><th>行几</th><th>父</th><th>备注</th></tr>\n";
      for (std::size_t g = 1; g < groups.size(); ++g) {
        for (const Person* p : groups[g]) {
          h << "<tr><td>" << (g - 1) << "</td><td>" << html_escape(p->family_name + p->given_name)
            << "</td><td>" << html_escape(p->courtesy_name.value_or(""))
            << (p->art_name ? " " + html_escape(*p->art_name) : "") << "</td><td>"
            << html_escape(life_span(*p)) << "</td><td>"
            << (p->birth_order ? std::to_string(*p->birth_order) : "") << "</td><td>"
            << html_escape(p->father_id.value_or("")) << "</td><td>"
            << html_escape(p->note.value_or("")) << "</td></tr>\n";
        }
      }
      h << "</table>\n";
      break;
    case BookTemplate::Genealogy:
      for (std::size_t g = 1; g < groups.size(); ++g) {
        if (groups[g].empty()) continue;
        h << "<h2 class='gen'>第" << (g - 1) << "世</h2><div class='genealogy'>";
        for (const Person* p : groups[g]) {
          h << html_escape(p->family_name + p->given_name)
            << "（" << html_escape(life_span(*p)) << "）<br/>";
        }
        h << "</div>\n";
      }
      break;
    case BookTemplate::Biography:
      for (std::size_t g = 1; g < groups.size(); ++g) {
        if (groups[g].empty()) continue;
        h << "<h2 class='gen'>第" << (g - 1) << "世</h2>";
        for (const Person* p : groups[g]) {
          h << "<div class='bio'><h3>" << html_escape(p->family_name + p->given_name)
            << " <span class='note'>" << html_escape(life_span(*p)) << "</span></h3>"
            << "<div>" << html_escape(p->biography.value_or("")) << "</div></div>\n";
        }
      }
      break;
  }
  if (!grouped.invalid_note.empty()) {
    h << "<div class='note'>注：" << html_escape(grouped.invalid_note)
      << "（未列入世系，请修正世代序号）</div>\n";
  }
  h << "<p class='note'>本谱由 genea 生成 · 打印时请在浏览器选择「另存为 PDF」"
    << "并启用背景图形。</p>\n</body></html>\n";
  return h.str();
}

}  // namespace genealogy::io
