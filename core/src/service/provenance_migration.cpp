#include <genealogy/service/provenance_migration.hpp>

#include <algorithm>
#include <cstring>
#include <regex>

#include <genealogy/model/person.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/service/citation_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/source_service.hpp>
#include <genealogy/repository/transaction.hpp>

namespace genealogy {

namespace {

// 出处 token：谱pNN / 谱 pNN / 谱pNN-NN。谱 与 p 之间容许一个空格；
// 裸 pNN（无 谱 前缀）不匹配——据 p100、p55 待终核这类旁注不迁移。
const std::regex& token_pattern() {
  static const std::regex pattern("谱[ ]*p[ ]*[0-9]+(-[0-9]+)?");
  return pattern;
}

bool note_has_doubt(const std::string& text) {
  // 存疑/待核标记（转录工作台的 [存疑，待核原卷]、「墨淡待核」等）。
  return text.find("存疑") != std::string::npos ||
         text.find("待核") != std::string::npos;
}

// 提取文本里全部 出处 token（保持出现顺序，person 内去重）。
std::vector<std::string> extract_tokens(const std::string& text) {
  std::vector<std::string> out;
  auto begin = std::sregex_iterator(text.begin(), text.end(),
                                    token_pattern());
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    const std::string token = it->str();
    if (std::find(out.begin(), out.end(), token) == out.end()) {
      out.push_back(token);
    }
  }
  return out;
}

// 剥离 note 开头连续的出处 token 及紧随的分隔符。返回是否有改动。
bool strip_leading_tokens(std::string& note) {
  static const std::regex separator("[；;：:，,、 ]+");
  bool changed = false;
  std::smatch m;
  for (;;) {
    if (!std::regex_search(note, m, token_pattern()) || m.position(0) != 0) {
      break;
    }
    note = note.substr(static_cast<std::size_t>(m.length(0)));
    changed = true;
    // 剥掉紧随的分隔符（「谱p71；正文」→「正文」）
    std::smatch sep;
    if (std::regex_search(note, sep, separator) && sep.position(0) == 0) {
      note = note.substr(static_cast<std::size_t>(sep.length(0)));
    }
  }
  return changed;
}

}  // namespace

Result<ProvenanceMigrationReport> migrate_note_provenance(
    Database& db, const ProvenanceSourceBinding& binding,
    bool strip_note_tokens, bool dry_run) {
  ProvenanceMigrationReport report;
  BookRepository books(db);
  auto book = books.get();
  if (!book) return book.error();

  SourceService sources(db);
  CitationService citations(db);
  PersonService persons(db);
  PersonRepository person_repo(db);

  // 来源登记（懒创建）：单谱单库恒单来源，title 精确匹配即复用。
  // 绑定缺省 title = 谱名 + 「家谱」——book.name 即谱名原文，已是
  // 《高阳李氏家谱》形态的照录，不再追加后缀。
  std::string source_id;
  bool source_known = false;
  auto ensure_source = [&]() -> Result<std::string> {
    if (source_known) return source_id;
    const std::string title = binding.title.empty() ? book->name : binding.title;
    SourceFilter f;
    f.title_contains = title;
    f.limit = 200;
    auto existing = sources.list_sources(f);
    if (!existing) return existing.error();
    const auto hit = std::find_if(
        existing->begin(), existing->end(),
        [&](const Source& s) { return s.title == title; });
    if (hit != existing->end()) {
      ++report.sources_reused;
      source_id = hit->id;
      source_known = true;
      return source_id;
    }
    if (dry_run) {
      ++report.sources_created;
      source_known = true;
      source_id = "";
      return source_id;
    }
    Source src;
    src.type = SourceType::GenealogyBook;
    src.title = title;
    if (!binding.year_text.empty()) src.year_text = binding.year_text;
    src.repository = binding.repository;
    src.author = binding.author;
    auto made = sources.create_source(src);
    if (!made) return made.error();
    ++report.sources_created;
    source_id = made->id;
    source_known = true;
    return source_id;
  };

  // 事务：citation 与 note 剥离要么整体生效要么整体不动。
  std::optional<Transaction> tx;
  if (!dry_run) {
    tx.emplace(db);
    if (tx->error()) return *tx->error();
  }

  // 人物扫描（游标翻页载到尽）
  std::optional<std::string> cursor;
  for (;;) {
    PersonFilter pf;
    pf.limit = 1000;
    pf.cursor = cursor;
    auto page = person_repo.list(pf);
    if (!page) return page.error();
    if (page->empty()) break;

    for (auto& person : *page) {
      ++report.persons_scanned;
      auto bound = ensure_source();
      if (!bound) {
        report.warnings.push_back("来源登记失败，跳过: " + bound.error().message());
        continue;
      }
      const std::string& source_id = *bound;
      const bool doubt = note_has_doubt(person.note.value_or(std::string{})) ||
                         note_has_doubt(person.biography.value_or(std::string{}));

      // note + biography 的 token → citation（三键去重）
      std::vector<std::string> tokens = extract_tokens(
          person.note.value_or(std::string{}) + " " +
          person.biography.value_or(std::string{}));
      bool cited_here = false;
      for (const auto& token : tokens) {
        CitationFilter cf;
        cf.subject_type = CitationSubjectType::Person;
        cf.subject_id = person.id;
        cf.source_id = source_id;
        cf.limit = 1000;
        auto existing = citations.list_citations(cf);
        if (!existing) return existing.error();
        const auto hit = std::find_if(
            existing->begin(), existing->end(),
            [&](const Citation& c) {
              return c.locator && *c.locator == token;
            });
        if (hit != existing->end()) {
          ++report.citations_deduped;
          continue;
        }
        Citation draft;
        draft.subject_type = CitationSubjectType::Person;
        draft.subject_id = person.id;
        draft.source_id = source_id;
        draft.locator = token;
        draft.confidence = doubt ? Confidence::Low : Confidence::High;
        if (doubt) ++report.low_confidence;
        if (dry_run) {
          ++report.citations_created;
          cited_here = true;
          continue;
        }
        auto made = citations.create_citation(draft);
        if (!made) return made.error();
        ++report.citations_created;
        cited_here = true;
      }
      if (cited_here) ++report.persons_cited;

      // note 剥离开头出处 token（biography 不动）。单条更新失败
      // 记 warning 继续——一条脏数据不该让 385 条迁移整体回滚
      if (strip_note_tokens && person.note) {
        std::string stripped = *person.note;
        if (strip_leading_tokens(stripped)) {
          ++report.notes_stripped;
          if (dry_run) {
            if (stripped.empty()) person.note = std::nullopt;
            else person.note = stripped;
            continue;
          }
          Person p = person;
          if (stripped.empty()) p.note = std::nullopt;
          else p.note = stripped;
          auto updated = persons.update_person(p);
          if (!updated) {
            report.warnings.push_back("note 剥离更新失败（保留原文）: " +
                                      person.id + " — " +
                                      updated.error().message());
            --report.notes_stripped;
            continue;
          }
        }
      }
    }
    cursor = page->back().id;
  }

  if (tx) {
    if (auto cr = tx->commit(); !cr) return cr.error();
  }
  return report;
}

nlohmann::json to_json(const ProvenanceMigrationReport& r) {
  nlohmann::json warnings = nlohmann::json::array();
  for (const auto& w : r.warnings) warnings.push_back(w);
  return nlohmann::json{
      {"persons_scanned", r.persons_scanned},
      {"persons_cited", r.persons_cited},
      {"sources_created", r.sources_created},
      {"sources_reused", r.sources_reused},
      {"citations_created", r.citations_created},
      {"citations_deduped", r.citations_deduped},
      {"notes_stripped", r.notes_stripped},
      {"low_confidence", r.low_confidence},
      {"warnings", std::move(warnings)},
  };
}

}  // namespace genealogy
