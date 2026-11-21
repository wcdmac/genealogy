// 出处迁移用例测试（7.6 第三道验收门「证据链覆盖关键事实」执行体）。
//
// 覆盖真实评测库的 note 形态：单页码 / 区间 / 多 token / 存疑标记 /
// biography 出处 / 正文内引用（不剥离）/ 幂等重跑 / 未绑定谱跳过。
// 迁移走 SourceService/CitationService/PersonService——顺带验证
// M7.2 域在真实数据形态下的行为（这正是本轮迁移的另一半目的）。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/source.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/source_repository.hpp>
#include <genealogy/service/provenance_migration.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-test-" + generate_ulid() + ".db");
}

void cleanup_db(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + "-wal", ec);
  std::filesystem::remove(path.string() + "-shm", ec);
}

Person mk(const std::string& given, const std::string& note = "",
          const std::string& biography = "") {
  Person p;
  p.family_name = "李";
  p.given_name = given;
  p.gender = Gender::Male;
  if (!note.empty()) p.note = note;
  if (!biography.empty()) p.biography = biography;
  return p;
}

}  // namespace

TEST_CASE("出处迁移：单页码/区间/存疑/剥字与幂等") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  Database& db = *opened;

  PersonRepository persons(db);
  auto p1 = persons.insert(mk("文远", "谱p74"));
  auto p2 = persons.insert(mk("承嗣", "谱p99-100"));
  auto p3 = persons.insert(
      mk("敬启", "谱p171：卒康熙四十二年；[存疑，待核原卷]"));
  auto p4 = persons.insert(mk("无出处"));
  REQUIRE(p1);
  REQUIRE(p2);
  REQUIRE(p3);
  REQUIRE(p4);

  // 第一跑（keep-note）：citation 全建、note 原样——工作台仍在写
  // note，迁移可先挂链后清文
  auto report = migrate_note_provenance(db, {}, false, false);
  REQUIRE(report);
  CHECK(report->sources_created == 1);  // 默认宗族一谱一来源
  CHECK(report->citations_created == 3);
  CHECK(report->persons_cited == 3);
  CHECK(report->notes_stripped == 0);
  CHECK(report->low_confidence == 1);  // p3 存疑标记 → low
  CHECK(report->warnings.empty());

  // citation 三键正确
  CitationRepository citations(db);
  auto c1 = citations.list(CitationFilter{});
  REQUIRE(c1);
  CHECK(c1->size() == 3);
  bool seen_range = false;
  int high = 0;
  int low = 0;
  for (const auto& c : *c1) {
    CHECK(c.subject_type == CitationSubjectType::Person);
    if (c.locator && *c.locator == "谱p99-100") seen_range = true;
    if (c.confidence == Confidence::Low) ++low;
    if (c.confidence == Confidence::High) ++high;
  }
  CHECK(seen_range);
  CHECK(high == 2);
  CHECK(low == 1);

  // 幂等复跑 + 剥离：token 仍在 note，citation 三键去重 0 新增，
  // 这次把开头的出处剥掉（先挂链后清文的第二阶段）
  auto again = migrate_note_provenance(db, {}, true, false);
  REQUIRE(again);
  CHECK(again->citations_created == 0);
  CHECK(again->citations_deduped == 3);
  CHECK(again->notes_stripped == 3);
  CHECK(again->sources_created == 0);
  auto c2 = citations.list(CitationFilter{});
  REQUIRE(c2);
  CHECK(c2->size() == 3);

  // note 剥离语义：纯出处 → NULL；带正文 → 保留正文
  auto got1 = persons.get(p1->id);
  REQUIRE(got1);
  CHECK_FALSE(got1->note.has_value());  // 「谱p74」剥光即清空
  auto got3 = persons.get(p3->id);
  REQUIRE(got3);
  REQUIRE(got3->note.has_value());
  CHECK(got3->note->find("卒康熙四十二年") == 0);        // 正文保留
  CHECK(got3->note->find("谱p171") == std::string::npos);  // 出处已剥离
  auto got4 = persons.get(p4->id);
  REQUIRE(got4);
  CHECK_FALSE(got4->note.has_value());  // 无出处者不动


  // 全清后的终跑：note 已无 token → 三项全零（收敛态）
  auto third = migrate_note_provenance(db, {}, true, false);
  REQUIRE(third);
  CHECK(third->citations_created == 0);
  CHECK(third->citations_deduped == 0);
  CHECK(third->notes_stripped == 0);

  cleanup_db(path);
}

TEST_CASE("出处迁移：biography 出处照迁且不剥原文；正文引用保留") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  Database& db = *opened;

  PersonRepository persons(db);
  auto p1 = persons.insert(
      mk("国梁嗣子", "谱p80", "六世，国梁公之子（谱p80，墨淡待核）。"));
  auto p2 = persons.insert(
      mk("维勤", "谱p171：墓志铭（陈廷敬撰，谱p55-60）载其世系"));
  REQUIRE(p1);
  REQUIRE(p2);

  auto report = migrate_note_provenance(db, {});
  REQUIRE(report);
  // p1: note 谱p80 + bio 谱p80（同人同 token 去重 → 1 条）
  // p2: note 谱p171 + bio 无 + 正文内 谱p55-60（带谱前缀 → 照迁）
  CHECK(report->citations_created == 3);
  CHECK(report->low_confidence == 1);  // 仅 p1（墨淡待核）→ low；p2 无存疑

  CitationRepository citations(db);
  auto rows = citations.list(CitationFilter{});
  REQUIRE(rows);
  bool bio_token = false;
  bool prose_token = false;
  for (const auto& c : *rows) {
    if (c.locator && *c.locator == "谱p80") bio_token = true;
    if (c.locator && *c.locator == "谱p55-60") prose_token = true;
  }
  CHECK(bio_token);
  CHECK(prose_token);  // 正文内带谱前缀的引用照迁

  // biography 原文不剥
  auto got = persons.get(p1->id);
  REQUIRE(got);
  REQUIRE(got->biography.has_value());
  CHECK(got->biography->find("谱p80") != std::string::npos);
  // p2 note：开头 token 剥离，正文里的 谱p55-60 原样保留
  auto got2 = persons.get(p2->id);
  REQUIRE(got2);
  REQUIRE(got2->note.has_value());
  CHECK(got2->note->rfind("谱p171", 0) != 0);  // 不再以出处开头
  CHECK(got2->note->find("谱p55-60") != std::string::npos);

  cleanup_db(path);
}

TEST_CASE("出处迁移：dry-run 零写入；keep-note 保留原文") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  Database& db = *opened;

  PersonRepository persons(db);
  auto p1 = persons.insert(mk("文远", "谱p74"));
  REQUIRE(p1);

  auto dry = migrate_note_provenance(db, {}, true, true);
  REQUIRE(dry);
  CHECK(dry->citations_created == 1);
  CHECK(dry->notes_stripped == 1);
  CitationRepository citations(db);
  auto rows = citations.list(CitationFilter{});
  REQUIRE(rows);
  CHECK(rows->empty());  // dry-run 零写入
  auto got = persons.get(p1->id);
  REQUIRE(got);
  REQUIRE(got->note.has_value());
  CHECK(*got->note == "谱p74");  // note 原样

  // keep-note：citation 照建，note 不剥
  auto kept = migrate_note_provenance(db, {}, false, false);
  REQUIRE(kept);
  CHECK(kept->citations_created == 1);
  CHECK(kept->notes_stripped == 0);
  auto got2 = persons.get(p1->id);
  REQUIRE(got2);
  REQUIRE(got2->note.has_value());
  CHECK(*got2->note == "谱p74");

  cleanup_db(path);
}

TEST_CASE("出处迁移：多谱库按 lineage 各建一来源；显式绑定覆盖") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  Database& db = *opened;

  // 建第二个谱
  
  PersonRepository persons(db);
  auto li = persons.insert(mk("李一人", "谱p12"));
  Person wp;
  wp.family_name = "吴";
  wp.given_name = "吴一人";
  wp.gender = Gender::Male;
  wp.note = "谱p33";
  auto wu_p = persons.insert(wp);
  REQUIRE(li);
  REQUIRE(wu_p);

  // 显式绑定：李氏带收藏机构
  ProvenanceSourceBinding li_binding;
  li_binding.title = "高阳李氏家谱";
  li_binding.year_text = "清嘉庆四年钞本";
  li_binding.repository = "上海图书馆";

  auto report = migrate_note_provenance(db, {li_binding});
  REQUIRE(report);
  CHECK(report->sources_created == 2);  // 李氏显式 + 吴氏派生
  CHECK(report->citations_created == 2);
  CHECK(report->warnings.empty());  // 两谱都有绑定

  // 各来源独立：两人物引用不同 source_id
  CitationRepository citations(db);
  auto rows = citations.list(CitationFilter{});
  REQUIRE(rows);
  CHECK(rows->size() == 2);
  CHECK((*rows)[0].source_id != (*rows)[1].source_id);

  // 派生来源的 title/year 从谱名解析
  SourceRepository sources(db);
  auto srcs = sources.list(SourceFilter{});
  REQUIRE(srcs);
  CHECK(srcs->size() == 2);
  for (const auto& s : *srcs) {
    CHECK(s.type == SourceType::GenealogyBook);
    if (s.title == "高阳李氏家谱") {
      REQUIRE(s.repository.has_value());
      CHECK(*s.repository == "上海图书馆");
    }
    if (s.title == "长林吴氏家谱") {
      REQUIRE(s.year_text.has_value());
      CHECK(*s.year_text == "乾隆四年刻本");
    }
  }

  cleanup_db(path);
}
