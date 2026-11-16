// M7.4 Document 域测试：CRUD 闭环 + 中文正文 FTS 检索（ROADMAP 7.4
// 验收门「文档可按正文检索命中」的单元级保障）。
//
// trigram 的三个命中面都要打中：全词、子串、1-2 字 LIKE 回退。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/document.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/document_service.hpp>
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

Document mk(const std::string& title, const std::string& content,
            DocumentType type = DocumentType::Preface) {
  Document d;
  d.lineage_id = kDefaultLineageId;
  d.type = type;
  d.title = title;
  d.content = content;
  return d;
}

int count_type(const std::vector<Document>& docs, DocumentType t) {
  int n = 0;
  for (const auto& d : docs) {
    if (d.type == t) ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("文档：CRUD 闭环 + 更新触发 FTS 同步") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(opened->schema_version() >= 4);  // 0004 document_fts 已应用
  REQUIRE(ensure_default_lineage(*opened));

  DocumentService svc(*opened);

  // 建档
  auto made = svc.create_document(mk("嘉庆四年修谱序",
                                     "我族自豫章迁徙中湘，建德星堂以祀先。"));
  REQUIRE(made.has_value());
  REQUIRE(!made->id.empty());

  // 读回：可选字段缺省不出现
  auto got = svc.get_document(made->id);
  REQUIRE(got.has_value());
  CHECK(got->title == "嘉庆四年修谱序");
  CHECK_FALSE(got->author.has_value());
  CHECK_FALSE(got->seq.has_value());

  // 更新：正文变化后 FTS 必须同步（AU 触发器），新词立即可检
  Document changed = *got;
  changed.content = "凡我族人，谨守家训，辑睦宗族。";
  changed.author = "陈文远";
  changed.seq = 1;
  auto saved = svc.update_document(changed);
  REQUIRE(saved.has_value());
  CHECK(saved->seq.has_value());
  CHECK(*saved->seq == 1);

  auto hits = svc.search_documents("谨守家训", 50);
  REQUIRE(hits.has_value());
  CHECK(hits->size() == 1);
  CHECK(hits->front().id == made->id);

  // 列表过滤
  auto all = svc.list_documents(DocumentFilter{});
  REQUIRE(all.has_value());
  CHECK(all->size() == 1);
  DocumentFilter by_type;
  by_type.type = DocumentType::Contract;
  auto contracts = svc.list_documents(by_type);
  REQUIRE(contracts.has_value());
  CHECK(count_type(*contracts, DocumentType::Contract) == 0);

  // 删除 → FTS 同步清空（AD 触发器）
  REQUIRE(svc.remove_document(made->id));
  auto after = svc.search_documents("谨守家训", 50);
  REQUIRE(after.has_value());
  CHECK(after->empty());
  auto gone = svc.get_document(made->id);
  CHECK_FALSE(gone.has_value());
  CHECK(gone.error().code() == ErrorCode::NotFound);

  cleanup_db(path);
}

TEST_CASE("文档：中文正文检索——全词/子串/两字回退/无命中") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(ensure_default_lineage(*opened));

  DocumentService svc(*opened);
  auto made = svc.create_document(
      mk("义庄规条", "德星堂义庄条约十二条，凡我族人谨守。", DocumentType::Contract));
  REQUIRE(made.has_value());

  // 全词命中（trigram）
  auto full = svc.search_documents("义庄", 50);
  REQUIRE(full.has_value());
  CHECK(full->size() == 1);

  // 子串命中（trigram 的核心价值：unicode61 下不可能）
  auto sub = svc.search_documents("堂义庄条", 50);
  REQUIRE(sub.has_value());
  CHECK(sub->size() == 1);
  CHECK(sub->front().id == made->id);

  // 两字查询回退 LIKE（trigram 需 ≥3 字符）
  auto two = svc.search_documents("族人", 50);
  REQUIRE(two.has_value());
  CHECK(two->size() == 1);

  // 无命中：空结果、不报错
  auto none = svc.search_documents("碑刻文献", 50);
  REQUIRE(none.has_value());
  CHECK(none->empty());

  // 标题也能命中（FTS 索引含 title/author/content 三列）
  auto by_title = svc.search_documents("规条", 50);
  REQUIRE(by_title.has_value());
  CHECK(by_title->size() == 1);

  // 空关键词显式报错（不静默返回全量）
  auto empty = svc.search_documents("", 50);
  CHECK_FALSE(empty.has_value());
  CHECK(empty.error().code() == ErrorCode::InvalidArgument);

  cleanup_db(path);
}

TEST_CASE("文档：服务层校验——lineage 必须存在、标题必填") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(ensure_default_lineage(*opened));

  DocumentService svc(*opened);

  Document bad_lineage = mk("序", "正文");
  bad_lineage.lineage_id = "01NOTEXIST00000000000000000";
  auto r1 = svc.create_document(bad_lineage);
  CHECK_FALSE(r1.has_value());
  CHECK(r1.error().code() == ErrorCode::NotFound);

  Document no_title = mk("", "正文");
  auto r2 = svc.create_document(no_title);
  CHECK_FALSE(r2.has_value());
  CHECK(r2.error().code() == ErrorCode::InvalidArgument);

  Document no_lineage = mk("序", "正文");
  no_lineage.lineage_id = "";
  auto r3 = svc.create_document(no_lineage);
  CHECK_FALSE(r3.has_value());
  CHECK(r3.error().code() == ErrorCode::InvalidArgument);

  cleanup_db(path);
}
