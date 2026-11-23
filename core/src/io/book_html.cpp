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
      default: out += c;
    }
  }
  return out;
}

std::string read_binary(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
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

// 按世代分组，族内按出生序/ID 稳定排序。
std::vector<std::vector<const Person*>> by_generation(
    const std::vector<Person>& persons) {
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
  for (const Person* p : sorted) {
    const int g = generation_of(*p);
    if (groups.size() <= static_cast<std::size_t>(g + 1)) groups.resize(g + 2);
    groups[g + 1].push_back(p);
  }
  return groups;
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
    << "@top-center{content:'" << html_escape(title) << "';}"
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
  const auto groups = by_generation(opt.persons ? *opt.persons : empty_persons);
  switch (opt.tmpl) {
    case BookTemplate::Register:
      h << "<h2 class='gen'>世系表</h2><table class='register'>"
        << "<tr><th>世代</th><th>姓名</th><th>字/号</th><th>生卒</th><th>行几</th><th>父</th><th>备注</th></tr>\n";
      for (std::size_t g = 1; g < groups.size(); ++g) {
        for (const Person* p : groups[g]) {
          h << "<tr><td>" << g << "</td><td>" << html_escape(p->family_name + p->given_name)
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
        h << "<h2 class='gen'>第" << g << "世</h2><div class='genealogy'>";
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
        h << "<h2 class='gen'>第" << g << "世</h2>";
        for (const Person* p : groups[g]) {
          h << "<div class='bio'><h3>" << html_escape(p->family_name + p->given_name)
            << " <span class='note'>" << html_escape(life_span(*p)) << "</span></h3>"
            << "<div>" << html_escape(p->biography.value_or("")) << "</div></div>\n";
        }
      }
      break;
  }
  h << "<p class='note'>本谱由 genea 生成 · 打印时请在浏览器选择「另存为 PDF」"
    << "并启用背景图形。</p>\n</body></html>\n";
  return h.str();
}

}  // namespace genealogy::io
