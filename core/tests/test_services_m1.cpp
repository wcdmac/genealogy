// M1 用例层测试：五个 Service 的领域校验（支派归属、成员存在性、
// 主体存在性、宗族存在性）与基本 CRUD。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/branch_service.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/union_service.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-svc-" + generate_ulid() + ".db");
}

void cleanup_db(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + "-wal", ec);
  std::filesystem::remove(path.string() + "-shm", ec);
}

// 格式合法但保证不存在的 ULID。注意不能用 26 个 0——那是
const std::string kMissingId(26, 'Z');

struct Fixture {
  Database& db() { return *db_; }
  Fixture() : path(temp_db_path()) {
    auto opened = Database::open(path);
    REQUIRE(opened);
    db_ = std::move(opened);
  }
  ~Fixture() { cleanup_db(path); }

  // 快捷建档（落入默认宗族）
  Person make_person(const std::string& family, const std::string& given) {
    PersonService svc(*db_);
    Person p;
    p.family_name = family;
    p.given_name = given;
    auto r = svc.create_person(p);
    REQUIRE(r);
    return *r;
  }

  std::filesystem::path path;
  Result<Database> db_{Error{ErrorCode::Internal, "未初始化"}};
};

}  // namespace

TEST_CASE("宗族服务：必填校验与 CRUD", "[service][lineage]") {
  Fixture fx;
  l.commandery = "颍川";
  REQUIRE(created);
  REQUIRE(is_valid_ulid(created->id));
  REQUIRE(created->rev == 1);

  Lineage no_name;
  no_name.surname = "陈";
  REQUIRE_FALSE(bad1);
  REQUIRE(bad1.error().code() == ErrorCode::InvalidArgument);

  Lineage no_surname;
  no_surname.name = "无名谱";
  REQUIRE_FALSE(bad2);
  REQUIRE(bad2.error().code() == ErrorCode::InvalidArgument);

  const auto got = svc.get_lineage(created->id);
  REQUIRE(got);
  REQUIRE(got->commandery.value() == "颍川");

  // 更新后旧 rev 冲突
  Lineage upd = *created;
  upd.hall_name = "德星堂";
  const auto updated = svc.update_lineage(upd);
  REQUIRE(updated);
  REQUIRE(updated->rev == 2);
  REQUIRE(updated->hall_name.value() == "德星堂");

  Lineage stale = upd;  // rev 仍为 1
  const auto conflict = svc.update_lineage(stale);
  REQUIRE_FALSE(conflict);
  REQUIRE(conflict.error().code() == ErrorCode::Conflict);

  REQUIRE(svc.remove_lineage(created->id));
  REQUIRE_FALSE(svc.get_lineage(created->id));
}

TEST_CASE("支派服务：归属与存在性校验", "[service][branch]") {
  Fixture fx;
  BranchService svc(fx.db());
  REQUIRE(lin);

  Lineage other;
  other.name = "李氏谱";
  other.surname = "李";
  REQUIRE(lin2);

  const Person founder = fx.make_person("陈", "文远");

  // 正常建档
  Branch b;
  b.name = "东八房";
  b.founder_person_id = founder.id;
  b.generation_char = "明";
  const auto created = svc.create_branch(b);
  REQUIRE(created);
  REQUIRE(created->generation_char.value() == "明");

  // 宗族不存在
  Branch bad_lineage = b;
  const auto r1 = svc.create_branch(bad_lineage);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::NotFound);

  // 始迁祖不存在
  Branch bad_founder = b;
  bad_founder.founder_person_id = kMissingId;
  const auto r2 = svc.create_branch(bad_founder);
  REQUIRE_FALSE(r2);
  REQUIRE(r2.error().code() == ErrorCode::NotFound);

  // 父房不存在
  Branch bad_parent = b;
  bad_parent.parent_branch_id = kMissingId;
  const auto r3 = svc.create_branch(bad_parent);
  REQUIRE_FALSE(r3);
  REQUIRE(r3.error().code() == ErrorCode::NotFound);

  // 跨宗族父房：归属校验
  Branch cross = b;
  cross.parent_branch_id = created->id;    // 陈氏东八房
  const auto r4 = svc.create_branch(cross);
  REQUIRE_FALSE(r4);
  REQUIRE(r4.error().code() == ErrorCode::InvalidArgument);

  // 同宗族父房合法
  Branch child = b;
  child.name = "东八房长房";
  child.parent_branch_id = created->id;
  const auto r5 = svc.create_branch(child);
  REQUIRE(r5);

  // 列表按宗族筛选
  BranchFilter f;
  const auto list = svc.list_branches(f);
  REQUIRE(list);
  REQUIRE(list->size() == 2);
}

TEST_CASE("地点服务：必填与查询", "[service][place]") {
  Fixture fx;
  PlaceService svc(fx.db());

  Place p;
  p.name = "中湘";
  p.type = PlaceType::County;
  p.modern_admin = "湖南省湘潭县";
  const auto created = svc.create_place(p);
  REQUIRE(created);

  Place no_name;
  no_name.type = PlaceType::Village;
  const auto bad = svc.create_place(no_name);
  REQUIRE_FALSE(bad);
  REQUIRE(bad.error().code() == ErrorCode::InvalidArgument);

  PlaceFilter f;
  f.name_contains = "中湘";
  const auto list = svc.list_places(f);
  REQUIRE(list);
  REQUIRE(list->size() == 1);
  REQUIRE(list->at(0).modern_admin.value() == "湖南省湘潭县");
}

TEST_CASE("结合服务：成员存在性与去重", "[service][union]") {
  Fixture fx;
  PlaceService places(fx.db());
  UnionService svc(fx.db());

  const Person husband = fx.make_person("陈", "文远");
  const Person wife = fx.make_person("王", "婉容");

  Place home;
  home.name = "德星堂";
  const auto place = places.create_place(home);
  REQUIRE(place);

  Union u;
  u.type = UnionType::Marriage;
  u.members.push_back(UnionMember{husband.id, MemberRole::Husband});
  u.members.push_back(UnionMember{wife.id, MemberRole::Wife});
  u.place_id = place->id;
  const auto created = svc.create_union(u);
  REQUIRE(created);
  REQUIRE(created->members.size() == 2);

  // 成员不存在
  Union bad_member = u;
  bad_member.members.push_back(
      UnionMember{kMissingId, MemberRole::Concubine});
  const auto r1 = svc.create_union(bad_member);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::NotFound);

  // 成员重复
  Union dup = u;
  dup.members.push_back(UnionMember{wife.id, MemberRole::Concubine});
  const auto r2 = svc.create_union(dup);
  REQUIRE_FALSE(r2);
  REQUIRE(r2.error().code() == ErrorCode::InvalidArgument);

  // 空成员
  Union empty;
  const auto r3 = svc.create_union(empty);
  REQUIRE_FALSE(r3);
  REQUIRE(r3.error().code() == ErrorCode::InvalidArgument);

  // 结合地点不存在
  Union bad_place = u;
  bad_place.place_id = kMissingId;
  const auto r4 = svc.create_union(bad_place);
  REQUIRE_FALSE(r4);
  REQUIRE(r4.error().code() == ErrorCode::NotFound);

  // 按人物检索结合
  UnionFilter f;
  f.person_id = husband.id;
  const auto list = svc.list_unions(f);
  REQUIRE(list);
  REQUIRE(list->size() == 1);
}

TEST_CASE("事件服务：主体存在性与类型", "[service][event]") {
  Fixture fx;
  UnionService unions(fx.db());
  EventService svc(fx.db());

  const Person p = fx.make_person("陈", "文远");
  REQUIRE(lin);

  // 人物事件（葬）
  Event burial;
  burial.subject_type = EventSubjectType::Person;
  burial.subject_id = p.id;
  burial.type = EventType::Burial;
  burial.date = GenealogyDate::from_iso("1901-03-15").value();
  burial.description = "葬祖茔亥山巳向";
  const auto created = svc.create_event(burial);
  REQUIRE(created);
  REQUIRE(created->description.value() == "葬祖茔亥山巳向");

  // 主体不存在
  Event bad_subject = burial;
  bad_subject.subject_id = kMissingId;
  const auto r1 = svc.create_event(bad_subject);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::NotFound);

  // 宗族事件
  Event lineage_event;
  lineage_event.subject_type = EventSubjectType::Lineage;
  lineage_event.subject_id = lin->id;
  lineage_event.type = EventType::Annotation;
  lineage_event.description = "六修谱告成";
  const auto r2 = svc.create_event(lineage_event);
  REQUIRE(r2);

  // 按主体检索
  EventFilter f;
  f.subject_type = EventSubjectType::Person;
  f.subject_id = p.id;
  const auto list = svc.list_events(f);
  REQUIRE(list);
  REQUIRE(list->size() == 1);
  REQUIRE(list->at(0).type == EventType::Burial);
}

TEST_CASE("人物服务：宗族存在性校验", "[service][person]") {
  Fixture fx;
  PersonService svc(fx.db());

  // 不存在的宗族
  Person bad;
  bad.family_name = "陈";
  bad.given_name = "文远";
  const auto r1 = svc.create_person(bad);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::NotFound);

  // 合法建档
  Person good;
  good.family_name = "陈";
  good.given_name = "文远";
  const auto r2 = svc.create_person(good);
  REQUIRE(r2);

  // 父亲不存在
  Person orphan = good;
  orphan.given_name = "文英";
  orphan.father_id = kMissingId;
  const auto r3 = svc.create_person(orphan);
  REQUIRE_FALSE(r3);
  REQUIRE(r3.error().code() == ErrorCode::NotFound);
}
