// GUI 数据层无头测试。
//
// 覆盖范围刻意停在「不依赖 ImGui/GLFW 的部分」：DbSession 的开关库、
// 六个面板共用的数据装配（人物 CRUD / 检索 / 建图 / 关系计算 / 校验
// 装载）。渲染层（GLFW 开窗）需要显示环境，由 --headless-check 在
// 真机上单独验证，不进这里——否则 CI 无显示服务器必红。
//
// 与 core 测试的分工：core 测用例层本身；这里测「GUI 怎么用用例层」
// ——筛选条件怎么填、图怎么建、错误怎么冒到状态栏。M2 时
// PersonGraph 的 use-after-free 正是这一类错误（GUI 用法错了，
// core 单测全绿）。

#include <catch2/catch_test_macros.hpp>

#include <genealogy/gui/db_session.hpp>

#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/ulid.hpp>
#include <genealogy/validate/validation.hpp>

#include <filesystem>
#include <string>

using namespace genealogy;
using namespace genealogy::gui;
using namespace genealogy::lineage;

namespace {

// 每个用例一份临时库，跑完即删
struct TempDb {
  std::filesystem::path path;
  DbSession session;

  TempDb() {
    path = std::filesystem::temp_directory_path() /
           ("genealogy-gui-" + generate_ulid() + ".db");
  }
  ~TempDb() {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + "-wal", ec);
    std::filesystem::remove(path.string() + "-shm", ec);
  }
  bool open(std::string* why) { return session.open(path.string(), why); }
};

Person make_person(const std::string& family, const std::string& given,
                   Gender g, const std::string& birth_year) {
  Person p;
  p.lineage_id = kDefaultLineageId;
  p.family_name = family;
  p.given_name = given;
  p.gender = g;
  if (!birth_year.empty()) {
    p.birth_date = *GenealogyDate::from_iso(birth_year);
  }
  return p;
}

}  // namespace

TEST_CASE("DbSession：开库、建档、读取、关闭") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));

  auto made = t.session.persons().create_person(make_person("陈", "文远", Gender::Male, "1864"));
  REQUIRE(made.has_value());
  const std::string id = made->id;

  auto got = t.session.persons().get_person(id);
  REQUIRE(got.has_value());
  CHECK(got->family_name == "陈");
  CHECK(got->given_name == "文远");
  CHECK(got->birth_date.gregorian_year() == 1864);

  t.session.close();
  CHECK_FALSE(t.session.is_open());
  // 关库后服务不可用语义由调用方保证（is_open 前置检查）
}

TEST_CASE("DbSession：坏路径报错且不带半截状态") {
  DbSession session;
  std::string why;
  // 指向一个不存在的目录：打开必须失败并给出原因。
  // 必须用正斜杠+不存在的目录链：反斜杠在 POSIX 上只是文件名字符，
  // "Z:\\no\\such\\dir\\x.db" 会被 SQLite 当成合法相对路径建出文件
  // （2026-11-14 macOS CI 实测 open 成功致测试失败）。
  const bool ok = session.open("no/such/dir/x.db", &why);
  CHECK_FALSE(ok);
  CHECK_FALSE(why.empty());
  CHECK_FALSE(session.is_open());
}

TEST_CASE("面板数据装配：人物更新带 rev 乐观锁") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto made = t.session.persons().create_person(make_person("陈", "文远", Gender::Male, "1864"));
  REQUIRE(made.has_value());

  // 面板的编辑流：读 → 改字段 → update（rev 不变应成功）
  Person p = *made;
  p.given_name = "文远·改";
  auto saved = t.session.persons().update_person(p);
  REQUIRE(saved.has_value());
  CHECK(saved->given_name == "文远·改");
  CHECK(saved->rev > made->rev);

  // rev 用过期的值再改：必须 409，不允许静默覆盖
  auto stale = t.session.persons().update_person(p);
  CHECK_FALSE(stale.has_value());
  CHECK(stale.error().code() == ErrorCode::Conflict);
}

TEST_CASE("面板数据装配：检索命中与清空") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  REQUIRE(t.session.persons().create_person(make_person("陈", "文远", Gender::Male, "1864")).has_value());
  REQUIRE(t.session.persons().create_person(make_person("陈", "德昭", Gender::Male, "1901")).has_value());

  auto hits = t.session.persons().search_persons("德昭", 50);
  REQUIRE(hits.has_value());
  CHECK(hits->size() == 1);
  CHECK((*hits)[0].given_name == "德昭");

  auto none = t.session.persons().search_persons("不存在的名字", 50);
  REQUIRE(none.has_value());
  CHECK(none->empty());
}

TEST_CASE("面板数据装配：建图 + 关系路径 + 称谓 + 五服") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  // 三代：祖父 → 父 → 子，外加父之妻
  auto grandpa = t.session.persons().create_person(make_person("陈", "大业", Gender::Male, "1830"));
  auto father = t.session.persons().create_person(make_person("陈", "守业", Gender::Male, "1860"));
  auto son = t.session.persons().create_person(make_person("陈", "文远", Gender::Male, "1890"));
  REQUIRE(grandpa.has_value());
  REQUIRE(father.has_value());
  REQUIRE(son.has_value());

  Person f = *father;
  f.father_id = grandpa->id;
  REQUIRE(t.session.persons().update_person(f).has_value());
  Person s = *son;
  s.father_id = father->id;
  REQUIRE(t.session.persons().update_person(s).has_value());

  PersonFilter pf;
  pf.limit = 1000;
  auto persons = t.session.persons().list_persons(pf);
  REQUIRE(persons.has_value());
  UnionFilter uf;
  uf.limit = 1000;
  auto unions = t.session.unions().list_unions(uf);
  REQUIRE(unions.has_value());
  const PersonGraph g = PersonGraph::build(std::move(*persons), std::move(*unions));

  const Relationship rel = find_relationship(g, son->id, grandpa->id);
  REQUIRE(rel.found);
  CHECK(rel.generation_gap() == -2);   // 孙子对祖父：尊两辈

  AppellationQuery aq;
  aq.ego = son->id;
  aq.alter = grandpa->id;
  auto app = compute_appellation(g, aq);
  REQUIRE(app.has_value());
  CHECK(app->standard == "祖父");
  CHECK_FALSE(app->elder == false);    // elder = true

  MourningQuery mq;
  mq.ego = son->id;
  mq.alter = grandpa->id;
  auto md = compute_mourning_degree(g, mq);
  REQUIRE(md.has_value());
  // 普通孙为祖父母 = 齐衰不杖期 13 月；承重孙（嫡嗣承祖父之宗）
  // 才是斩衰 27 月——两条分支都在规则表里（shared/rules/
  // mourning_ming_qing.json 的 F.F 两条），这里都验到。
  CHECK(md->rule_id == "F.F");
  CHECK(md->period_months == 13);
  CHECK(std::string(to_string(md->grade)) == "qicui_buzhang");

  // 把孙子改成承重孙再算：应跳变到斩衰三年。
  // 注意必须重新读：上面给 son 设 father_id 时 rev 已 +1，
  // 拿创建时的副本去更新只会拿到 409（乐观锁）——这正是 rev 机制
  // 要防的「基于过期快照写回」，测试里也按真实用法走。
  auto son_now = t.session.persons().get_person(son->id);
  REQUIRE(son_now.has_value());
  Person heir = *son_now;
  heir.is_heir = true;
  REQUIRE(t.session.persons().update_person(heir).has_value());
  PersonFilter pf2;
  pf2.limit = 1000;
  auto persons2 = t.session.persons().list_persons(pf2);
  REQUIRE(persons2.has_value());
  const PersonGraph g2 = PersonGraph::build(std::move(*persons2),
                                            std::vector<Union>{});
  auto md2 = compute_mourning_degree(g2, mq);
  REQUIRE(md2.has_value());
  CHECK(md2->period_months == 27);
  CHECK(std::string(to_string(md2->grade)) == "zhancui");
}

TEST_CASE("面板数据装配：十代树展开与节点计数") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  // 五代单链：每代一个儿子
  std::string parent_id;
  std::vector<std::string> ids;
  for (int i = 0; i < 5; ++i) {
    Person p = make_person("陈", "世" + std::to_string(i), Gender::Male,
                           std::to_string(1850 + i * 25));
    if (!parent_id.empty()) p.father_id = parent_id;
    auto made = t.session.persons().create_person(p);
    REQUIRE(made.has_value());
    parent_id = made->id;
    ids.push_back(made->id);
  }

  PersonFilter pf;
  pf.limit = 1000;
  auto persons = t.session.persons().list_persons(pf);
  REQUIRE(persons.has_value());
  const PersonGraph g = PersonGraph::build(std::move(*persons),
                                           std::vector<Union>{});
  auto tree = descendants_tree(g, ids.front(), 10);
  REQUIRE(tree.has_value());
  CHECK(tree_node_count(*tree) == 5);
  CHECK(tree_depth(*tree) == 5);   // 根 + 四代
}

TEST_CASE("面板数据装配：校验引擎装载路径") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  // 故意造一个时间悖论（出生晚于卒年），校验必须报 R1
  Person bad = make_person("陈", "悖论", Gender::Male, "1900");
  bad.death_date = *GenealogyDate::from_iso("1850");
  REQUIRE(t.session.persons().create_person(bad).has_value());

  PersonFilter pf;
  pf.limit = 1000;
  auto persons = t.session.persons().list_persons(pf);
  REQUIRE(persons.has_value());
  const PersonGraph g = PersonGraph::build(std::move(*persons),
                                           std::vector<Union>{});
  auto report = validate(g, nullptr, ValidationOptions{});
  REQUIRE(report.has_value());
  CHECK(report->error_count >= 1);
  bool has_r1 = false;
  for (const auto& i : report->issues) {
    if (i.rule_id == "R1") has_r1 = true;
  }
  CHECK(has_r1);
}
