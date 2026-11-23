// 排版导出面板的无 UI 状态机：组装输入、调 core 排版引擎、写文件。

#include <genealogy/gui/panels/export_panel.hpp>

#include <genealogy/io/book_html.hpp>
#include <genealogy/io/chart_svg.hpp>
#include <genealogy/io/layout_svg.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/service/book_service.hpp>
#include <genealogy/io/markup.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/union_repository.hpp>

#include <fstream>

namespace genealogy::gui {

std::string ExportPanel::build_markup(DbSession& session,
                                       const std::string& focus_id) const {
  std::string out;
  if (focus_id.empty()) return out;
  auto focus = session.persons().get_person(focus_id);
  if (!focus) return out;

  out += "〔卷〕" + focus->family_name + "家谱\n";
  out += "〔章〕" + focus->family_name + focus->given_name + "世家\n";

  // 焦点本人 + 子女（有父子边的）作为正文，行传作夹注
  auto write_person = [&](const Person& p) {
    std::string line = p.family_name + p.given_name;
    if (p.courtesy_name && !p.courtesy_name->empty())
      line += "，字" + *p.courtesy_name;
    out += line + "。";
    if (p.biography && !p.biography->empty())
      out += "〔注〕" + *p.biography + "〔/注〕";
    out += "〔圈〕\n";
  };
  write_person(*focus);

  auto all = session.persons().list_persons(PersonFilter{});
  if (all) {
    for (const auto& p : *all) {
      if (p.father_id && *p.father_id == focus_id) write_person(p);
    }
  }
  return out;
}

bool ExportPanel::generate_layout(const std::string& markup,
                                  const std::string& preset,
                                  const std::string& body_size,
                                  const std::string& title,
                                  std::string* out_svg, std::string* why) const {
  auto parsed = genealogy::io::parse_markup(markup);
  if (!parsed) {
    *why = parsed.error().message();
    return false;
  }
  genealogy::io::LayoutOptions opt;
  opt.preset = preset.empty() ? "guji_classic" : preset;
  opt.book_title = title.empty() ? "家谱" : title;
  opt.content = parsed->nodes;
  if (!body_size.empty()) {
    auto geo = genealogy::io::geometry_for_preset(opt.preset);
    if (geo) {
      auto pj = genealogy::io::layout_preset_json(opt.preset);
      if (pj) {
        auto j = nlohmann::json::parse(*pj, nullptr, false);
        if (!j.is_discarded()) {
          j["typography"]["body_size"] = body_size;
          opt.geometry_override = genealogy::io::LayoutGeometry::from_preset(j);
        }
      }
    }
  }
  auto svg = genealogy::io::render_layout_page(opt);
  if (!svg) {
    *why = svg.error().message();
    return false;
  }
  *out_svg = *svg;
  return true;
}

bool ExportPanel::generate_chart(DbSession& session, const std::string& style,
                                 const std::string& focus_id, int depth,
                                 std::string* out_svg, std::string* why) const {
  PersonRepository people(session.db());
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = people.list(pf);
  if (!persons) { *why = persons.error().message(); return false; }
  UnionRepository unions(session.db());
  UnionFilter uf;
  uf.limit = 200000;
  auto us = unions.list(uf);
  if (!us) { *why = us.error().message(); return false; }
  auto graph = lineage::PersonGraph::build(std::move(*persons), std::move(*us));
  io::ChartOptions opt;
  opt.graph = &graph;
  auto parsed = io::chart_style_from_string(style);
  if (parsed) opt.style_hint = *parsed;
  opt.focus_id = focus_id;
  opt.depth = depth;
  opt.title = "世系图";
  auto chart = io::render_chart_svg(opt);
  if (!chart) { *why = chart.error().message(); return false; }
  out_svg->clear();
  for (const auto& page : chart->pages) *out_svg += page.svg;
  return true;
}

bool ExportPanel::generate_book(DbSession& session, std::string* out_html,
                                std::string* why) const {
  genealogy::BookService books(session.db());
  auto book = books.get_book();
  if (!book) { *why = book.error().message(); return false; }
  PersonFilter pf;
  pf.limit = 200000;
  auto persons = session.persons().list_persons(pf);
  if (!persons) { *why = persons.error().message(); return false; }
  io::BookHtmlOptions opt;
  opt.book = &*book;
  opt.persons = &*persons;
  auto html = io::render_book_html(opt);
  if (!html) { *why = html.error().message(); return false; }
  *out_html = *html;
  return true;
}

bool ExportPanel::write_file(const std::string& path,
                             const std::string& content,
                             std::string* why) const {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    *why = "写不出文件: " + path;
    return false;
  }
  out << content;
  return true;
}

}  // namespace genealogy::gui
