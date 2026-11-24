// 迁徙领域测试（M5-4）：仓储往返 + 服务层必填校验 + 外键约束。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>

#include <genealogy/error.hpp>
#include <genealogy/model/migration.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/migration_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/place_repository.hpp>
#include <genealogy/service/migration_service.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-migration-" + generate_ulid() + ".db");
}

// Result::value() 返回左值引用，而 Database 不可拷贝——用 helper
// 移动取出（失败即测试环境坏，直接抛，不用 REQUIRE 绕）。
Database open_db(const std::filesystem::path& path) {
  auto opened = Database::open(path);
  if (!opened) {
    throw std::runtime_error("打开测试库失败: " + opened.error().message());
  }
  return std::move(*opened);
}

struct Fixture {
  std::filesystem::path path = temp_db_path();
  Database db{open_db(path)};
  PlaceRepository places{db};
  PersonRepository persons{db};
  MigrationRepository migrations{db};
  MigrationService service{db};

  Fixture() { }
  ~Fixture() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + "-wal", ec);
    std::filesystem::remove(path.string() + "-shm", ec);
  }

  Place make_place(const std::string& name, double lat, double lng) {
    Place p;
    p.name = name;
    p.type = PlaceType::Village;
    p.lat = lat;
    p.lng = lng;
    return places.insert(p).value();
  }

  Person make_person(const std::string& given) {
    Person p;
    p.family_name = "陈";
    p.given_name = given;
    p.gender = Gender::Male;
    return persons.insert(p).value();
  }
};

}  // namespace

TEST_CASE_METHOD(Fixture, "迁徙：建档后读回，字段无损") {
  const Place from = make_place("中湘", 27.83, 112.94);
  const Place to = make_place("渝州", 29.56, 106.55);
  const Person person = make_person("文远");

  Migration m;
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = person.id;
  m.from_place_id = from.id;
  m.to_place_id = to.id;
  m.date = GenealogyDate::from_raw("同治三年").value();
  m.reason = "避乱";

  const auto saved = migrations.insert(m).value();
  CHECK(saved.id.size() == 26);  // ULID

  const auto got = migrations.get(saved.id).value();
  CHECK(got.subject_type == MigrationSubjectType::Person);
  CHECK(got.subject_id == person.id);
  CHECK(got.from_place_id == from.id);
  CHECK(got.to_place_id == to.id);
  CHECK(got.date.has_value());
  CHECK(got.date->lunar == "同治三年");
  CHECK(got.reason == "避乱");
}

TEST_CASE_METHOD(Fixture, "迁徙：按主体检索，不串主体") {
  const Place to = make_place("滇南", 25.04, 102.71);
  const Person a = make_person("甲");
  const Person b = make_person("乙");

  Migration ma;
  ma.subject_type = MigrationSubjectType::Person;
  ma.subject_id = a.id;
  ma.to_place_id = to.id;
  migrations.insert(ma);

  Migration mb;
  mb.subject_type = MigrationSubjectType::Person;
  mb.subject_id = b.id;
  mb.to_place_id = to.id;
  migrations.insert(mb);

  MigrationFilter fa;
  fa.subject_type = MigrationSubjectType::Person;
  fa.subject_id = a.id;
  const auto rows = migrations.list(fa).value();
  CHECK(rows.size() == 1);
  CHECK(rows.front().subject_id == a.id);
}

TEST_CASE_METHOD(Fixture, "迁徙：支派主体与个人主体可共存") {
  const Place to = make_place("岭南", 23.13, 113.26);

  Migration branch_move;
  branch_move.subject_type = MigrationSubjectType::Branch;
  branch_move.subject_id = "BRANCH01";
  branch_move.to_place_id = to.id;
  const auto saved = migrations.insert(branch_move).value();

  const auto got = migrations.get(saved.id).value();
  CHECK(got.subject_type == MigrationSubjectType::Branch);
  CHECK(got.subject_id == "BRANCH01");
}

TEST_CASE_METHOD(Fixture, "迁徙服务层：缺目的地拒绝建档") {
  const Person person = make_person("丙");

  Migration m;
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = person.id;
  // 故意不给 to_place_id

  const auto result = service.create_migration(m);
  CHECK(!result.has_value());
  CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE_METHOD(Fixture, "迁徙服务层：目的地不存在被外键拦下") {
  const Person person = make_person("丙");

  Migration m;
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = person.id;
  m.to_place_id = "NOSUCHPLACE00000000000000";  // DDL 有 REFERENCES place(id)

  const auto result = service.create_migration(m);
  CHECK(!result.has_value());
}

TEST_CASE_METHOD(Fixture, "迁徙服务层：subject_id 无外键约束（多态主体的设计事实）") {
  // subject_id 按 subject_type 指向 person 或 branch，DDL 无法表达
  // 多态外键，故不设 REFERENCES；存在性由校验规则兜底，不靠库约束。
  const Place to = make_place("衡州", 26.90, 112.57);

  Migration m;
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = "MAYBE_NOT_EXIST0000000000";
  m.to_place_id = to.id;

  const auto result = service.create_migration(m);
  CHECK(result.has_value());
  CHECK(result->subject_id == "MAYBE_NOT_EXIST0000000000");
}

TEST_CASE_METHOD(Fixture, "迁徙：更新与删除") {
  const Place to = make_place("江南", 31.23, 121.47);
  const Person person = make_person("丁");

  Migration m;
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = person.id;
  m.to_place_id = to.id;
  const auto saved = migrations.insert(m).value();

  Migration updated = saved;
  updated.reason = "营商";
  const auto after = migrations.update(updated).value();
  CHECK(after.reason == "营商");

  CHECK(migrations.remove(saved.id).has_value());
  CHECK(!migrations.get(saved.id).has_value());
  CHECK(!migrations.remove(saved.id).has_value());  // 重复删报不存在
}

TEST_CASE_METHOD(Fixture, "审查修复回归：迁徙 update 与 create 共用必填校验") {
  const Place place = make_place("测试地", 0, 0);

  Migration m;
  m.id = generate_ulid();
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = generate_ulid();  // subject 无外键，测试必填校验足够
  m.to_place_id = place.id;
  auto created = service.create_migration(m);
  REQUIRE(created);

  // update 清空必填的 to_place_id 应被拒绝（此前 update 不校验）
  Migration bad = *created;
  bad.to_place_id.reset();
  auto rejected = service.update_migration(bad);
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().code() == ErrorCode::InvalidArgument);
}
