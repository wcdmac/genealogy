#include <genealogy/service/provenance_migration.hpp>

#include <algorithm>
#include <cstring>
#include <regex>

#include <genealogy/model/person.hpp>
#include <genealogy/repository/lineage_repository.hpp>
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

// 从 lineage 名派生默认 Source 绑定：去括号 + 家谱后缀；括号内原文
// 作 year_text。例：高阳李氏（嘉庆四年钞本）→《高阳李氏家谱》/
// 嘉庆四年钞本。
ProvenanceSourceBinding derive_binding(const Lineage& lineage) {
  ProvenanceSourceBinding b;
  b.lineage_id = lineage.id;
  std::string name = lineage.name;
  const auto paren_open = name.find("（");
  if (paren_open != std::string::npos) {
    const std::string paren = name.substr(paren_open + std::strlen("（"));
    const auto paren_close = paren.rfind("）");
    if (paren_close != std::string::npos) {
      b.year_text = paren.substr(0, paren_close);
    }
    name = name.substr(0, paren_open);
  }
  b.title = name + "家谱";
  return b;
}

}  // namespace

Result<ProvenanceMigrationReport> migrate_note_provenance(
    Database& db, const std::vector<ProvenanceSourceBinding>& bindings,
    bool strip_note_tokens, bool dry_run) {
  ProvenanceMigrationReport report;
  LineageRepository lineages(db);
  LineageFilter lf;
  lf.limit = 200;
  auto lineage_rows = lineages.list(lf);
  if (!lineage_rows) return lineage_rows.error();

  // lineage_id → 绑定：显式 bindings 优先，其余自动派生
  std::vector<ProvenanceSourceBinding> resolved;
  for (const auto& lineage : *lineage_rows) {
    const auto it = std::find_if(
        bindings.begin(), bindings.end(),
        [&](const ProvenanceSourceBinding& b) {
          return b.lineage_id == lineage.id;
        });
    resolved.push_back(it != bindings.end() ? *it : derive_binding(lineage));
  }

  SourceService sources(db);
  CitationService citations(db);
  PersonService persons(db);
  PersonRepository person_repo(db);

  // 来源登记（懒创建）：首条引用真正需要时才查/建——空谱（如默认
  // 宗族）不产生占位来源。title 精确匹配即复用（幂等）。
  std::vector<std::pair<std::string, std::string>> source_by_lineage;
  auto source_for_or_create =
      [&](const ProvenanceSourceBinding& binding) -> Result<std::string> {
    SourceFilter f;
    f.title_contains = binding.title;
    f.limit = 200;
    auto existing = sources.list_sources(f);
    if (!existing) return existing.error();
    const auto hit = std::find_if(
        existing->begin(), existing->end(),
        [&](const Source& s) { return s.title == binding.title; });
    if (hit != existing->end()) {
      ++report.sources_reused;
      return hit->id;
    }
    // dry-run 不建来源（此前守卫缺失，dry-run 曾把来源写进库）：
    // 计「拟建」数并返回空 id——dry-run 的引用计数用不到真 id
    if (dry_run) {
      ++report.sources_created;
      return std::string{};
    }
    Source src;
    src.type = SourceType::GenealogyBook;
    src.title = binding.title;
    if (!binding.year_text.empty()) src.year_text = binding.year_text;
    src.repository = binding.repository;
    src.author = binding.author;
    auto made = sources.create_source(src);
    if (!made) return made.error();
    ++report.sources_created;
    return made->id;
  };
  auto source_for = [&](const std::string& lineage_id) -> const std::string* {
    for (const auto& [lid, sid] : source_by_lineage) {
      if (lid == lineage_id) return &sid;
    }
    return nullptr;
  };
  auto ensure_source = [&](const std::string& lineage_id) -> Result<std::string> {
    if (const std::string* sid = source_for(lineage_id)) return *sid;
    const auto it = std::find_if(
        resolved.begin(), resolved.end(),
        [&](const ProvenanceSourceBinding& b) { return b.lineage_id == lineage_id; });
    if (it == resolved.end()) {
      return Error{ErrorCode::NotFound, "lineage 无来源绑定: " + lineage_id};
    }
    auto made = source_for_or_create(*it);
    if (!made) return made.error();
    source_by_lineage.emplace_back(lineage_id, *made);
    return *made;
  };

  // 事务：citation 与 note 剥离要么整体生效要么整体不动。
  // dry_run 无写操作，不开事务（M9.1：Transaction 嵌套感知）。
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
      auto bound = ensure_source(person.lineage_id);
      if (!bound) {
        report.warnings.push_back("lineage 无来源绑定，跳过: " +
                                  person.lineage_id);
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
        // 库内已有（同主体+来源+locator）→ 幂等跳过
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
            continue;  // 不写库
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
