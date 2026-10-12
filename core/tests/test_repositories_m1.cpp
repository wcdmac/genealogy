#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>

#include <genealogy/error.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/event_repository.hpp>
#include <genealogy/repository/lineage_repository.hpp>
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

TEST_CASE("宗族仓储：CRUD 与游标", "[repo][lineage]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  ensure_default_lineage(*opened);
  LineageRepository repo(*opened);

  Lineage l;
  l.name = "中湘陈氏六修族谱";
  l.surname = "陈";
  l.commandery = "颍川";
  l.hall_name = "德星堂";
  l.generation_poem = "明德崇仁厚，乾坤万世昌";
  const auto created = repo.insert(l);
  REQUIRE(created);
  REQUIRE(is_valid_ulid(created->id));
  REQUIRE(created->rev == 1);
  REQUIRE(created->commandery.value() == "颍川");

  const auto got = repo.get(created->id);
  REQUIRE(got);
  REQUIRE(got->name == "中湘陈氏六修族谱");
  REQUIRE(got->generation_poem.value() == "明德崇仁厚，乾坤万世昌");

  LineageFilter by_surname;
  by_surname.surname = "陈";
  const auto list = repo.list(by_surname);
  REQUIRE(list);
  REQUIRE(list->size() == 1);  // 默认宗族 surname 为空串，不命中

  // 空串姓筛选命中默认宗族（占位宗族，M1.2 上线 lineage 管理后淡化）
  LineageFilter by_empty;
  by_empty.surname = "";
  const auto defaults = repo.list(by_empty);
  REQUIRE(defaults);
  REQUIRE(defaults->size() == 1);
  REQUIRE(defaults->at(0).id == std::string(kDefaultLineageId));

  LineageFilter none;
  none.surname = "李";
  const auto empty = repo.list(none);
  REQUIRE(empty);
  REQUIRE(empty->empty());

  // 更新：入库时 rev=1，created.rev 亦为 1，故可更新
  // （注意：insert 按值传入，id/rev 只回写在 created 上，原 l.id 为空）
  const auto stale = repo.update(*created);
  REQUIRE(stale);
  REQUIRE(stale->rev == 2);

  Lineage bad_rev = *stale;
  bad_rev.rev = 1;
  const auto conflict = repo.update(bad_rev);
  REQUIRE_FALSE(conflict);
  REQUIRE(conflict.error().code() == ErrorCode::Conflict);

  REQUIRE(repo.remove(created->id));
  REQUIRE_FALSE(repo.get(created->id));
  REQUIRE_FALSE(repo.remove(created->id));

  cleanup_db(path);
}

TEST_CASE("支派仓储：归属与外键约束", "[repo][branch]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  ensure_default_lineage(*opened);
  LineageRepository lineages(*opened);
  BranchRepository repo(*opened);

  Lineage l;
  l.name = "测试宗族";
  l.surname = "陈";
  const auto lineage = lineages.insert(l);
  REQUIRE(lineage);

  Branch b;
  b.lineage_id = lineage->id;
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
  filter.lineage_id = lineage->id;
  const auto list = repo.list(filter);
  REQUIRE(list);
  REQUIRE(list->size() == 1);

  Branch bogus = b;
  bogus.lineage_id = std::string(26, 'Z');  // 不存在的宗族
  const auto bad = repo.insert(bogus);
  REQUIRE_FALSE(bad);
  REQUIRE(bad.error().code() == ErrorCode::Internal);  // 外键失败

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
  ensure_default_lineage(*opened);
  PersonService persons(*opened);
  UnionRepository repo(*opened);

  Person husband;
  husband.lineage_id = kDefaultLineageId;
  husband.family_name = "陈";
  husband.given_name = "文远";
  husband.gender = Gender::Male;
  const auto h = persons.create_person(husband);
  REQUIRE(h);

  Person wife;
  wife.lineage_id = kDefaultLineageId;
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
  ensure_default_lineage(*opened);
  PersonService persons(*opened);
  EventRepository repo(*opened);

  Person p;
  p.lineage_id = kDefaultLineageId;
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
