#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/service/person_service.hpp>
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

}  // namespace

TEST_CASE("仓储：建档/查询/更新/检索/删除 闭环", "[repo]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(opened->schema_version() == 1);
  ensure_default_lineage(*opened);

  PersonService svc(*opened);
  Person draft;
  draft.lineage_id = kDefaultLineageId;
  draft.family_name = "陈";
  draft.given_name = "文远";
  draft.gender = Gender::Male;
  draft.birth_date = *GenealogyDate::from_raw("同治三年四月初四");
  draft.biography = "始迁祖，明经业，葬祖茔亥山巳向。";

  const auto created = svc.create_person(draft);
  REQUIRE(created);
  REQUIRE(is_valid_ulid(created->id));
  REQUIRE(created->rev == 1);
  REQUIRE_FALSE(created->created_at.empty());

  const auto got = svc.get_person(created->id);
  REQUIRE(got);
  REQUIRE(got->family_name == "陈");
  REQUIRE(got->given_name == "文远");
  REQUIRE(got->birth_date.lunar.value() == "同治三年四月初四");
  REQUIRE(got->biography.has_value());

  PersonRepository repo(*opened);

  // 更新 + 乐观锁
  Person updated = *got;
  updated.courtesy_name = "伯远";
  const auto after = repo.update(updated);
  REQUIRE(after);
  REQUIRE(after->rev == 2);
  REQUIRE(after->courtesy_name.value() == "伯远");

  const auto stale = repo.update(*got);  // rev 仍是 1
  REQUIRE_FALSE(stale);
  REQUIRE(stale.error().code() == ErrorCode::Conflict);

  // FTS5 全文检索
  const auto found = svc.search_persons("文远", 10);
  REQUIRE(found);
  REQUIRE(found->size() == 1);
  REQUIRE((*found)[0].id == created->id);

  const auto by_bio = svc.search_persons("始迁祖", 10);
  REQUIRE(by_bio);
  REQUIRE(by_bio->size() == 1);

  const auto none = svc.search_persons("查无此人", 10);
  REQUIRE(none);
  REQUIRE(none->empty());

  // 游标分页
  PersonFilter filter;
  filter.limit = 1;
  const auto page1 = svc.list_persons(filter);
  REQUIRE(page1);
  REQUIRE(page1->size() == 1);
  filter.cursor = (*page1)[0].id;
  const auto page2 = svc.list_persons(filter);
  REQUIRE(page2);
  REQUIRE(page2->empty());

  // 删除
  REQUIRE(repo.remove(created->id));
  const auto gone = svc.get_person(created->id);
  REQUIRE_FALSE(gone);
  REQUIRE(gone.error().code() == ErrorCode::NotFound);
  REQUIRE_FALSE(repo.remove(created->id));

  cleanup_db(path);
}

TEST_CASE("用例：建档校验", "[service]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  ensure_default_lineage(*opened);

  PersonService svc(*opened);

  Person missing_name;
  missing_name.lineage_id = kDefaultLineageId;
  missing_name.family_name = "陈";
  const auto r1 = svc.create_person(missing_name);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::InvalidArgument);

  Person bad_lineage = missing_name;
  bad_lineage.given_name = "某";
  bad_lineage.lineage_id.clear();
  const auto r2 = svc.create_person(bad_lineage);
  REQUIRE_FALSE(r2);
  REQUIRE(r2.error().code() == ErrorCode::InvalidArgument);

  Person bad_father = bad_lineage;
  bad_father.lineage_id = kDefaultLineageId;
  bad_father.father_id = std::string(26, '7');  // ULID 格式合法但不存在
  const auto r3 = svc.create_person(bad_father);
  REQUIRE_FALSE(r3);
  REQUIRE(r3.error().code() == ErrorCode::NotFound);

  Person bad_father_format = bad_father;
  bad_father_format.father_id = "not-a-ulid";
  const auto r4 = svc.create_person(bad_father_format);
  REQUIRE_FALSE(r4);
  REQUIRE(r4.error().code() == ErrorCode::InvalidArgument);

  cleanup_db(path);
}
