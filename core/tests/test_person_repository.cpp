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
  REQUIRE(opened->schema_version() >= 1);  // 迁移数量随补丁增长（0002 起）
  REQUIRE(ensure_default_lineage(*opened));

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

TEST_CASE("仓储：parent_any 按父母任一过滤子女", "[repo]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(ensure_default_lineage(*opened));

  PersonRepository repo(*opened);
  auto make = [&](const std::string& given,
                  const std::optional<std::string>& father,
                  const std::optional<std::string>& mother) {
    Person p;
    p.lineage_id = kDefaultLineageId;
    p.family_name = "陈";
    p.given_name = given;
    p.gender = Gender::Male;
    p.father_id = father;
    p.mother_id = mother;
    auto made = repo.insert(p);
    REQUIRE(made);
    return made->id;
  };

  const std::string father = make("父", std::nullopt, std::nullopt);
  const std::string mother = make("母", std::nullopt, std::nullopt);
  const std::string son = make("子", father, mother);
  const std::string daughter_by_mother_only = make("女", std::nullopt, mother);

  // 父亲视角：只命中父亲登记的子女
  PersonFilter by_father;
  by_father.parent_any = father;
  by_father.limit = 100;
  const auto via_father = repo.list(by_father);
  REQUIRE(via_father);
  REQUIRE(via_father->size() == 1);
  CHECK((*via_father)[0].id == son);

  // 母亲视角：父亲未录的儿子 + 只挂母亲的女儿都能命中（OR 语义）
  PersonFilter by_mother;
  by_mother.parent_any = mother;
  by_mother.limit = 100;
  const auto via_mother = repo.list(by_mother);
  REQUIRE(via_mother);
  CHECK(via_mother->size() == 2);

  // 无父母的人物不受影响
  PersonFilter none;
  none.parent_any = make("无亲", std::nullopt, std::nullopt);
  none.limit = 100;
  const auto empty = repo.list(none);
  REQUIRE(empty);
  CHECK(empty->empty());

  cleanup_db(path);
}


TEST_CASE("用例：建档校验", "[service]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(ensure_default_lineage(*opened));

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
