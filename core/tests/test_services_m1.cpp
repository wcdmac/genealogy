// M1 用例层测试：五个 Service 的领域校验（支派归属、成员存在性、
// 主体存在性、宗族存在性）与基本 CRUD。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
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
#include <genealogy/service/attachment_service.hpp>
#include <genealogy/service/book_service.hpp>
#include <genealogy/service/branch_service.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/generation_poem_service.hpp>
#include <genealogy/service/merge_service.hpp>
#include <genealogy/service/migration_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/union_service.hpp>
#include <genealogy/model/attachment.hpp>
#include <genealogy/model/migration.hpp>
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
// 合法格式（首位 0..7、Crockford 字母表）但库中不存在的 ULID
const std::string kMissingId = "7ZZZZZZZZZZZZZZZZZZZZZZZZZ";

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

TEST_CASE("谱元数据服务：必填校验、更新与乐观锁（单谱单库）") {
  Fixture fx;
  BookService svc(fx.db());

  // 单谱单库：book 行由迁移建好，恒单行
  auto got = svc.get_book();
  REQUIRE(got.has_value());
  CHECK(got->rev == 1);

  // 更新必填字段
  Book upd = *got;
  upd.name = "中湘陈氏六修族谱";
  upd.surname = "陈";
  upd.commandery = "颍川";
  auto updated = svc.update_book(upd);
  REQUIRE(updated.has_value());
  CHECK(updated->rev == 2);
  REQUIRE(updated->commandery.has_value());
  CHECK(*updated->commandery == "颍川");

  // 单谱单库 book 恒单行，携带最新 rev 的连续更新递增而非冲突
  // （乐观锁在位：携带旧 rev 才冲突，见下方断言）
  Book again = *updated;
  again.hall_name = "德星堂";
  auto second = svc.update_book(again);
  REQUIRE(second.has_value());
  CHECK(second->rev == 3);
  REQUIRE(second->hall_name.has_value());
  CHECK(*second->hall_name == "德星堂");

  auto reread = svc.get_book();
  REQUIRE(reread.has_value());
  CHECK(reread->name == "中湘陈氏六修族谱");
  CHECK(reread->rev == 3);
}

TEST_CASE("支派服务：归属与存在性校验", "[service][branch]") {
  Fixture fx;
  BranchService svc(fx.db());


  const Person founder = fx.make_person("陈", "文远");

  // 正常建档
  Branch b;
  b.name = "东八房";
  b.founder_person_id = founder.id;
  b.generation_char = "明";
  const auto created = svc.create_branch(b);
  REQUIRE(created);
  REQUIRE(created->generation_char.value() == "明");

  // 单谱单库无 lineage 归属字段；支派名必填
  Branch no_name;
  const auto r1 = svc.create_branch(no_name);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::InvalidArgument);

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
  // 单谱单库：谱级事件（历史 Lineage 枚举）主体统一为字面量 book
  lineage_event.subject_id = "book";
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

TEST_CASE("人物服务：必填与父母存在性校验", "[service][person]") {
  Fixture fx;
  PersonService svc(fx.db());

  // 单谱单库无 lineage 归属；名必填
  Person bad;
  bad.family_name = "";
  bad.given_name = "文远";
  const auto r1 = svc.create_person(bad);
  REQUIRE_FALSE(r1);
  REQUIRE(r1.error().code() == ErrorCode::InvalidArgument);

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

TEST_CASE("谱元数据乐观锁：旧 rev 更新冲突（第四轮 P2-5）", "[service][book]") {
  Fixture fx;
  BookService svc(fx.db());
  auto got = svc.get_book();
  REQUIRE(got.has_value());
  Book stale = *got;
  stale.name = "并发方先改";
  stale.surname = "陈";  // 主姓必填（新库 book 行姓氏为空）
  auto win = svc.update_book(stale);
  REQUIRE(win.has_value());

  // 另一端还持有旧 rev 的副本，后写必须撞 409 而非静默覆盖
  Book stale2 = *got;
  stale2.name = "并发方后写";
  stale2.surname = "陈";
  auto conf = svc.update_book(stale2);
  REQUIRE_FALSE(conf.has_value());
  REQUIRE(conf.error().code() == ErrorCode::Conflict);

  auto reread = svc.get_book();
  REQUIRE(reread.has_value());
  CHECK(reread->name == "并发方先改");
}

TEST_CASE("人物祖先环校验：直指与多代绕回（第四轮 P1-5）", "[service][person]") {
  Fixture fx;
  PersonService svc(fx.db());

  auto self = fx.make_person("陈", "自指");
  Person p = *svc.get_person(self.id);
  p.father_id = self.id;  // 父亲=自己
  auto r1 = svc.update_person(p);
  REQUIRE_FALSE(r1.has_value());
  REQUIRE(r1.error().code() == ErrorCode::InvalidArgument);

  // 祖父链绕回：孙→祖父（合法），祖父→孙（成环）
  auto grand = fx.make_person("陈", "祖父");
  auto grandchild = fx.make_person("陈", "孙");
  Person gc = *svc.get_person(grandchild.id);
  gc.father_id = grand.id;
  REQUIRE(svc.update_person(gc).has_value());
  Person g = *svc.get_person(grand.id);
  g.father_id = grandchild.id;
  auto r2 = svc.update_person(g);
  REQUIRE_FALSE(r2.has_value());
  REQUIRE(r2.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("合并拒绝亲缘对（第四轮 P1-4）", "[service][merge]") {
  Fixture fx;
  PersonService persons(fx.db());
  auto father = fx.make_person("陈", "父");
  auto son = fx.make_person("陈", "子");
  Person s = *persons.get_person(son.id);
  s.father_id = father.id;
  REQUIRE(persons.update_person(s).has_value());

  MergeService merges(fx.db());
  // winner 的父亲=loser：合并会把 winner 改成自己的父亲
  auto r = merges.merge(son.id, father.id, false, "");
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().code() == ErrorCode::Conflict);
  // 反向（winner 是 loser 的子孙）同样拒绝
  auto r2 = merges.merge(father.id, son.id, false, "");
  REQUIRE_FALSE(r2.has_value());
  REQUIRE(r2.error().code() == ErrorCode::Conflict);
}

TEST_CASE("谱级派语 owner_id 归一 book（第四轮 P2）", "[service][poem]") {
  Fixture fx;
  GenerationPoemService svc(fx.db());
  // 历史调用方传任意 lineage id：服务必须归一到 book，否则派语按
  // owner_id 分裂，get/check 用的又是另一份
  auto set = svc.set_poem(PoemOwner::Lineage, "任意旧lineage编号", "仁义礼智信");
  REQUIRE(set.has_value());
  CHECK(set->size() == 5);
  auto got = svc.get_poem(PoemOwner::Lineage, "book");
  REQUIRE(got.has_value());
  REQUIRE(got->size() == 5);
  CHECK((*got)[0].character == "仁");
  CHECK((*got)[4].character == "信");
}

TEST_CASE("迁徙主体存在性校验（第四轮 P2）", "[service][migration]") {
  Fixture fx;
  PlaceService places(fx.db());
  Place dest;
  dest.name = "金陵";
  auto place = places.create_place(dest);
  REQUIRE(place.has_value());

  MigrationService svc(fx.db());
  Migration m;
  m.subject_type = MigrationSubjectType::Person;
  m.subject_id = kMissingId;  // 格式合法但不存在
  m.to_place_id = place->id;
  auto r = svc.create_migration(m);
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().code() == ErrorCode::NotFound);
}

TEST_CASE("地点删除保护：被引用时 409（第四轮 P2）", "[service][place]") {
  Fixture fx;
  PlaceService places(fx.db());
  Place draft;
  draft.name = "老宅";
  auto place = places.create_place(draft);
  REQUIRE(place.has_value());

  PersonService persons(fx.db());
  Person p;
  p.family_name = "陈";
  p.given_name = "宅主";
  p.birth_place_id = place->id;
  auto made = persons.create_person(p);
  REQUIRE(made.has_value());

  auto r = places.remove_place(place->id);
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().code() == ErrorCode::Conflict);

  // 解除引用后可删
  Person upd = *made;
  upd.birth_place_id.reset();
  REQUIRE(persons.update_person(upd).has_value());
  REQUIRE(places.remove_place(place->id).has_value());
}

TEST_CASE("人物删除级联清理附件（第四轮 P2）", "[service][attachment]") {
  Fixture fx;
  PersonService persons(fx.db());
  auto person = fx.make_person("陈", "有照");

  // 造一个真实源文件供 import_file 读取
  const auto src = std::filesystem::temp_directory_path() /
                   ("genealogy-att-" + generate_ulid() + ".dat");
  {
    std::ofstream out(src, std::ios::binary);
    out << "portrait-bytes";
  }

  AttachmentService att(fx.db());
  auto imported =
      att.import_file(src, AttachmentKind::Photo, "person", person.id, "");
  REQUIRE(imported.has_value());
  std::error_code rmec;
  std::filesystem::remove(src, rmec);

  AttachmentFilter f;
  f.subject_type = "person";
  f.subject_id = person.id;
  auto before = att.list(f);
  REQUIRE(before.has_value());
  CHECK(before->size() == 1);

  REQUIRE(persons.remove_person(person.id).has_value());
  auto after = att.list(f);
  REQUIRE(after.has_value());
  CHECK(after->empty());
}
