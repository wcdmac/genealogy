#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>

#include <genealogy/error.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/place_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-m1-" + generate_ulid() + ".db");
}

void cleanup_db(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + "-wal", ec);
  std::filesystem::remove(path.string() + "-shm", ec);
}

}  // namespace

TEST_CASE("谱元数据仓储：单行 get/update", "[repo][book]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  BookRepository repo(*opened);

  const auto got = repo.get();
  REQUIRE(got);
  CHECK(got->name.empty());

  Book b = *got;
  b.name = "中湘陈氏六修族谱";
  b.surname = "陈";
  b.commandery = "颍川";
  b.hall_name = "德星堂";
  b.generation_poem = "明德崇仁厚，乾坤万世昌";
  const auto updated = repo.update(b);
  REQUIRE(updated);
  CHECK(updated->name == "中湘陈氏六修族谱");
  CHECK(updated->generation_poem.value() == "明德崇仁厚，乾坤万世昌");
  CHECK(updated->rev == got->rev + 1);

  const auto stale = *updated;
  Book bad_rev = stale;
  bad_rev.rev = stale.rev;  // 同 rev 再 update 不会冲突（单行表无并发）
  const auto again = repo.update(bad_rev);
  REQUIRE(again);
  CHECK(again->rev > stale.rev);

  cleanup_db(path);
}
TEST_CASE("支派仓储：CRUD", "[repo][branch]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  BranchRepository repo(*opened);

  Branch b;
  b.name = "长房";
  b.generation_char = "明";
  const auto created = repo.insert(b);
  REQUIRE(created);
  REQUIRE(created->rev == 1);

  const auto got = repo.get(created->id);
  REQUIRE(got);
  REQUIRE(got->name == "长房");
  REQUIRE(got->generation_char.value() == "明");

  BranchFilter filter;
  const auto list = repo.list(filter);
  REQUIRE(list);
  REQUIRE(list->size() == 1);

  REQUIRE(repo.remove(created->id));
  REQUIRE_FALSE(repo.get(created->id));

  cleanup_db(path);
}

TEST_CASE("地点仓储：模糊检索与 LIKE 转义", "[repo][place]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  PlaceRepository repo(*opened);

  Place p;
  p.name = "江南苏州府吴县";
  p.type = PlaceType::Prefecture;
  p.modern_admin = "江苏省苏州市吴中区";
  p.lat = 31.32;
  p.lng = 120.63;
  const auto created = repo.insert(p);
  REQUIRE(created);
  REQUIRE(created->type.value() == PlaceType::Prefecture);
  REQUIRE(created->lat.value() == 31.32);

  PlaceFilter by_name;
  by_name.name_contains = "苏州";
  const auto hit = repo.list(by_name);
  REQUIRE(hit);
  REQUIRE(hit->size() == 1);

  PlaceFilter by_modern;
  by_modern.name_contains = "吴中";
  const auto hit2 = repo.list(by_modern);
  REQUIRE(hit2);
  REQUIRE(hit2->size() == 1);

  // LIKE 通配符注入必须被转义：% 不应匹配全部
  PlaceFilter wildcard;
  wildcard.name_contains = "%";
  const auto none = repo.list(wildcard);
  REQUIRE(none);
  REQUIRE(none->empty());

  PlaceFilter underscore;
  underscore.name_contains = "_";
  const auto none2 = repo.list(underscore);
  REQUIRE(none2);
  REQUIRE(none2->empty());

  Place updated = *created;
  updated.name = "苏州府";
  const auto after = repo.update(updated);
  REQUIRE(after);
  REQUIRE(repo.get(created->id)->name == "苏州府");

  REQUIRE(repo.remove(created->id));
  REQUIRE_FALSE(repo.get(created->id));

  cleanup_db(path);
}

TEST_CASE("婚姻结合仓储：成员事务/替换/级联删除", "[repo][union]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  PersonService persons(*opened);
  UnionRepository repo(*opened);

  Person husband;
  husband.family_name = "陈";
  husband.given_name = "文远";
  husband.gender = Gender::Male;
  const auto h = persons.create_person(husband);
  REQUIRE(h);

  Person wife;
  wife.family_name = "王";
  wife.given_name =  "婉娘";
  wife.gender = Gender::Female;
  const auto w = persons.create_person(wife);
  REQUIRE(w);

  Union u;
  u.type = UnionType::Marriage;
  u.sequence = 1;
  u.members = {{h->id, MemberRole::Husband}, {w->id, MemberRole::Wife}};
  const auto created = repo.insert(u);
  REQUIRE(created);
  REQUIRE(created->rev == 1);

  const auto got = repo.get(created->id);
  REQUIRE(got);
  REQUIRE(got->members.size() == 2);
  REQUIRE(got->sequence.value() == 1);

  UnionFilter by_person;
  by_person.person_id = h->id;
  const auto list = repo.list(by_person);
  REQUIRE(list);
  REQUIRE(list->size() == 1);
  // 回归：list 必须随行装载成员（曾漏装载，导致世系图丢配偶边）
  REQUIRE(list->at(0).members.size() == 2);
  REQUIRE(list->at(0).members.at(0).role == MemberRole::Husband);
  REQUIRE(list->at(0).members.at(1).role == MemberRole::Wife);

  // 无筛选全量列表同样带成员
  UnionFilter all;
  const auto all_list = repo.list(all);
  REQUIRE(all_list);
  REQUIRE(all_list->size() == 1);
  REQUIRE(all_list->at(0).members.size() == 2);

  // 成员不存在：外键失败，事务回滚，结合不落库
  Union bogus;
  bogus.members = {{std::string(26, '7'), MemberRole::Husband}};
  const auto bad = repo.insert(bogus);
  REQUIRE_FALSE(bad);

  // 更新：成员整体替换（只留妻方）
  Union updated = *got;
  updated.members = {{w->id, MemberRole::Wife}};
  const auto after = repo.update(updated);
  REQUIRE(after);
  REQUIRE(after->rev == 2);
  REQUIRE(repo.get(created->id)->members.size() == 1);

  Union stale = *after;
  stale.rev = 1;
  const auto conflict = repo.update(stale);
  REQUIRE_FALSE(conflict);
  REQUIRE(conflict.error().code() == ErrorCode::Conflict);

  // 删除：成员级联清理（DDL 无 ON DELETE CASCADE，仓储负责）
  REQUIRE(repo.remove(created->id));
  REQUIRE_FALSE(repo.get(created->id));
  UnionFilter after_remove;
  after_remove.person_id = w->id;
  const auto empty = repo.list(after_remove);
  REQUIRE(empty);
  REQUIRE(empty->empty());

  cleanup_db(path);
}

TEST_CASE("事件仓储：筛选与删除", "[repo][event]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  PersonService persons(*opened);
  EventRepository repo(*opened);

  Person p;
  p.family_name = "陈";
  p.given_name = "文远";
  const auto person = persons.create_person(p);
  REQUIRE(person);

  Event birth;
  birth.subject_type = EventSubjectType::Person;
  birth.subject_id = person->id;
  birth.type = EventType::Birth;
  birth.date = *GenealogyDate::from_iso("1864-05-09");
  birth.description = "生于同治三年四月初四";
  const auto created = repo.insert(birth);
  REQUIRE(created);

  Event burial;
  burial.subject_type = EventSubjectType::Person;
  burial.subject_id = person->id;
  burial.type = EventType::Burial;
  burial.description = "葬祖茔亥山巳向";
  REQUIRE(repo.insert(burial));

  EventFilter by_subject;
  by_subject.subject_type = EventSubjectType::Person;
  by_subject.subject_id = person->id;
  const auto all = repo.list(by_subject);
  REQUIRE(all);
  REQUIRE(all->size() == 2);

  EventFilter by_type;
  by_type.subject_type = EventSubjectType::Person;
  by_type.subject_id = person->id;
  by_type.type = EventType::Burial;
  const auto burials = repo.list(by_type);
  REQUIRE(burials);
  REQUIRE(burials->size() == 1);
  REQUIRE(burials->at(0).description.value() == "葬祖茔亥山巳向");

  REQUIRE(repo.remove(created->id));
  REQUIRE_FALSE(repo.get(created->id));
  const auto after = repo.list(by_subject);
  REQUIRE(after);
  REQUIRE(after->size() == 1);

  cleanup_db(path);
}
