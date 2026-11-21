// M7.1 名号域测试：CRUD + 主名互斥（服务层清他条 + partial unique
// index 库层兜底）+ 校验语义。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/person_name.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/person_name_service.hpp>
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

Person mk_person(const std::string& given) {
  Person p;
  p.family_name = "陈";
  p.given_name = given;
  p.gender = Gender::Male;
  return p;
}

PersonName mk_name(const std::string& person_id, const std::string& name,
                   PersonNameType type = PersonNameType::Infant) {
  PersonName n;
  n.person_id = person_id;
  n.name = name;
  n.type = type;
  return n;
}

int count_primary(const std::vector<PersonName>& rows) {
  int n = 0;
  for (const auto& r : rows) {
    if (r.is_primary) ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("名号：CRUD 闭环 + 主名互斥（置新主清旧主）") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  REQUIRE(opened->schema_version() >= 5);  // 0005 名号补列已应用

  PersonService persons(*opened);
  auto p = persons.create_person(mk_person("文远"));
  REQUIRE(p.has_value());

  PersonNameService svc(*opened);

  // 加乳名并置主
  auto made = svc.create_person_name(mk_name(p->id, "乳名阿远"));
  REQUIRE(made.has_value());
  REQUIRE(!made->id.empty());
  CHECK(made->type == PersonNameType::Infant);
  CHECK_FALSE(made->is_primary);

  // 置主
  PersonName primary = mk_name(p->id, "文远", PersonNameType::Genealogy);
  primary.is_primary = true;
  auto p2 = svc.create_person_name(primary);
  REQUIRE(p2.has_value());
  CHECK(p2->is_primary);

  // 再置一条主名：旧主自动降级（服务层纪律）
  PersonName school = mk_name(p->id, "学名振家", PersonNameType::School);
  school.is_primary = true;
  auto p3 = svc.create_person_name(school);
  REQUIRE(p3.has_value());
  CHECK(p3->is_primary);

  auto rows = svc.list_person_names(p->id);
  REQUIRE(rows.has_value());
  CHECK(rows->size() == 3);
  CHECK(count_primary(*rows) == 1);                     // 全人唯一
  CHECK(rows->front().id == p3->id);                    // 主名排最前
  CHECK(rows->front().type == PersonNameType::School);

  // 类型过滤语义在仓储排序里（is_primary DESC, id），列表含全部类型
  bool has_infant = false;
  for (const auto& r : *rows) {
    if (r.type == PersonNameType::Infant) has_infant = true;
  }
  CHECK(has_infant);

  // 删除主名 → 无主名状态合法（不强推任何一条上位）
  REQUIRE(svc.remove_person_name(p3->id));
  auto after = svc.list_person_names(p->id);
  REQUIRE(after.has_value());
  CHECK(count_primary(*after) == 0);
  CHECK(after->size() == 2);

  // 删除不存在 → 404
  auto gone = svc.remove_person_name("01NOTEXIST00000000000000000");
  CHECK_FALSE(gone.has_value());
  CHECK(gone.error().code() == ErrorCode::NotFound);

  cleanup_db(path);
}

TEST_CASE("名号：服务层校验——人物必须存在、名号文本必填") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);

  PersonNameService svc(*opened);

  auto no_person = svc.create_person_name(
      mk_name("01NOTEXIST00000000000000000", "阿远"));
  CHECK_FALSE(no_person.has_value());
  CHECK(no_person.error().code() == ErrorCode::NotFound);

  auto psvc = PersonService(*opened);
  auto p = psvc.create_person(mk_person("文远"));
  REQUIRE(p.has_value());

  auto no_name = svc.create_person_name(mk_name(p->id, ""));
  CHECK_FALSE(no_name.has_value());
  CHECK(no_name.error().code() == ErrorCode::InvalidArgument);

  auto no_pid = svc.create_person_name(mk_name("", "阿远"));
  CHECK_FALSE(no_pid.has_value());
  CHECK(no_pid.error().code() == ErrorCode::InvalidArgument);

  cleanup_db(path);
}
