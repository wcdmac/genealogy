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
#include <fstream>
#include <string>

#include <genealogy/gui/db_switch.hpp>
#include <genealogy/gui/panels/person_card.hpp>
#include <genealogy/gui/panels/person_table.hpp>
#include <genealogy/gui/panels/relation_panel.hpp>
#include <genealogy/gui/panels/search_panel.hpp>
#include <genealogy/gui/panels/tree_panel.hpp>
#include <genealogy/gui/panels/validation_panel.hpp>

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
  namespace fs = std::filesystem;
  DbSession session;
  std::string why;
  // Database::open 会自动 create_directories 父目录（首次开库的便利），
  // 「父目录不存在」不是坏路径——会被建出来然后打开成功（四跑 CI
  // 三平台实测；初版的 Z:\ 反斜杠路径在 POSIX 上更是合法文件名字符）。
  // 跨平台确定失败的坏路径用 ENOTDIR 语义：临时目录里先放一个普通
  // 文件 blocker，再把它当父目录开库。
  const fs::path dir = fs::temp_directory_path() / "genealogy-gui-test-badpath";
  fs::create_directories(dir);
  const fs::path blocker = dir / "blocker";
  {
    std::ofstream f(blocker, std::ios::binary);
    f << 'x';
  }
  const bool ok = session.open((blocker / "x.db").string(), &why);
  std::error_code ec;
  fs::remove_all(dir, ec);  // 清理临时目录，成功失败都不遗留
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
  auto report = validate(g, nullptr, nullptr, ValidationOptions{});
  REQUIRE(report.has_value());
  CHECK(report->error_count >= 1);
  bool has_r1 = false;
  for (const auto& i : report->issues) {
    if (i.rule_id == "R1") has_r1 = true;
  }
  CHECK(has_r1);
}

// ================================================================
// 面板状态机（ROADMAP「面板装配抽离 ImGui TU」债务偿还后的无头验证）。
// 第三轮修复的三个状态机自此可自动回归：dirty 置位与切换确认、
// 树面板深度重建条件与后台装载、换库缓存清理序列。

#include <chrono>
#include <thread>

namespace {

// 等后台面板线程收尾（本地毫秒级；超时判失败防止死等）
template <typename Pred>
void wait_until(Pred&& ready, int timeout_ms = 10000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (!ready()) {
    if (std::chrono::steady_clock::now() > deadline) {
      FAIL("wait_until 超时——后台面板线程未按预期收尾");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}  // namespace

TEST_CASE("面板状态机：人物卡 dirty 置位与三选切换（第三轮新-C3）") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto a = t.session.persons().create_person(
      make_person("陈", "文远", Gender::Male, "1864"));
  auto b = t.session.persons().create_person(
      make_person("陈", "承嗣", Gender::Male, "1890"));
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());

  AppState state;
  state.selected_person_id = a->id;
  PersonCardPanel card(state);

  // 装载：缓冲就位、无脏、无可确认切换
  REQUIRE(card.load(t.session));
  CHECK_FALSE(card.switch_pending(b->id));
  CHECK(card.has_cached_data());

  // 编辑置 dirty（第三轮修复前 dirty_ 从未被置 true——本断言即回归）
  card.edit_given_name("文远改");
  CHECK(card.switch_pending(b->id));
  CHECK_FALSE(card.switch_pending(a->id));  // 同一人物不构成切换

  // Stay：全局选中退回当前人物，缓冲保留
  state.selected_person_id = b->id;
  card.resolve_switch(PersonCardPanel::SwitchAction::Stay, t.session);
  CHECK(state.selected_person_id == a->id);
  CHECK(card.switch_pending(b->id));  // 仍是脏的

  // SaveAndLoad：先保存再切换——库里真改名，切换后 dirty 复位
  state.selected_person_id = b->id;
  card.resolve_switch(PersonCardPanel::SwitchAction::SaveAndLoad, t.session);
  auto saved = t.session.persons().get_person(a->id);
  REQUIRE(saved.has_value());
  CHECK(saved->given_name == "文远改");
  CHECK(state.selected_person_id == b->id);
  CHECK(card.has_cached_data());
  CHECK_FALSE(card.switch_pending(a->id));  // 已不脏也不指向 a

  // DiscardAndLoad：脏缓冲直接作废、按选中重载
  card.edit_given_name("不会入库的修改");
  state.selected_person_id = a->id;
  CHECK(card.switch_pending(a->id));
  card.resolve_switch(PersonCardPanel::SwitchAction::DiscardAndLoad, t.session);
  CHECK(card.has_cached_data());
  CHECK_FALSE(card.switch_pending(b->id));
  auto reloaded = t.session.persons().get_person(a->id);
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->given_name == "文远改");  // 丢弃的是缓冲，库值不变
}

TEST_CASE("面板状态机：人物卡保存走载入 rev 乐观锁（H11 回归）") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto made = t.session.persons().create_person(
      make_person("陈", "文远", Gender::Male, "1864"));
  REQUIRE(made.has_value());

  AppState state;
  state.selected_person_id = made->id;
  PersonCardPanel card(state);
  REQUIRE(card.load(t.session));
  card.edit_given_name("并发受害者");

  // 外部写入者抢先改库：rev 前进
  auto cur = t.session.persons().get_person(made->id);
  REQUIRE(cur.has_value());
  Person intruder = *cur;
  intruder.courtesy_name = "旁路写入";
  auto bumped = t.session.persons().update_person(intruder);
  REQUIRE(bumped.has_value());

  // 保存拿载入时的旧 rev → 409 冲突 → 原地不动 + 状态栏报错
  card.resolve_switch(PersonCardPanel::SwitchAction::SaveAndLoad, t.session);
  CHECK(state.status_is_error);
  CHECK(state.selected_person_id == made->id);  // 未被切走
  CHECK(card.has_cached_data());                // 编辑仍在（没丢）

  // 库里值未被本卡覆盖——仍是旁路写入者的 courtesy
  auto after = t.session.persons().get_person(made->id);
  REQUIRE(after.has_value());
  CHECK(after->courtesy_name == "旁路写入");
}

TEST_CASE("面板状态机：树面板重建条件含深度（第三轮新-C4）") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto made = t.session.persons().create_person(
      make_person("陈", "始祖", Gender::Male, "1600"));
  REQUIRE(made.has_value());

  AppState state;
  TreePanel tree(state);
  CHECK_FALSE(tree.has_cached_data());

  CHECK(tree.needs_rebuild(made->id, 10));  // 未装载过 → 重建
  tree.begin_load(t.session, made->id, 10);
  CHECK(tree.load_in_flight());
  wait_until([&] { return !tree.load_in_flight(); });
  tree.poll();
  CHECK(tree.tree_ready());
  CHECK(tree.has_cached_data());
  CHECK_FALSE(tree.needs_rebuild(made->id, 10));  // 同根同深 → 不重建
  CHECK(tree.needs_rebuild(made->id, 2));         // 深度变化 → 重建（修复前只看根）

  // 深度 1：只有根节点，不展开子女
  tree.begin_load(t.session, made->id, 1);
  wait_until([&] { return !tree.load_in_flight(); });
  tree.poll();
  CHECK(tree.tree_ready());
  CHECK(tree.last_error().empty());

  // 不存在的根：错误发布并被采纳，不残留旧树
  tree.begin_load(t.session, "01NOTEXIST0000000000000000XX", 5);
  wait_until([&] { return !tree.load_in_flight(); });
  tree.poll();
  CHECK_FALSE(tree.tree_ready());
  CHECK_FALSE(tree.last_error().empty());
  // 失败批次不采纳图与树；换库清理后再看缓存是否清空
  tree.on_database_closed();
  CHECK_FALSE(tree.has_cached_data());
}

TEST_CASE("面板状态机：换库作废在途装载（第三轮新-C10）") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto made = t.session.persons().create_person(
      make_person("陈", "始祖", Gender::Male, "1600"));
  REQUIRE(made.has_value());

  AppState state;
  TreePanel tree(state);
  tree.begin_load(t.session, made->id, 10);
  // 装载在途时换库：on_database_closed 必须先 join 再清场
  tree.on_database_closed();
  CHECK_FALSE(tree.load_in_flight());
  tree.poll();
  CHECK_FALSE(tree.has_cached_data());
}

TEST_CASE("面板状态机：校验面板后台跑与采纳（第三轮新-C10）") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto made = t.session.persons().create_person(
      make_person("陈", "无生年", Gender::Male, ""));
  REQUIRE(made.has_value());  // R10 生年缺失 info 触发器

  AppState state;
  ValidationPanel panel(state);
  CHECK_FALSE(panel.has_cached_data());
  panel.start_run(t.session);
  CHECK(panel.run_in_flight());
  wait_until([&] { return !panel.run_in_flight(); });
  panel.poll();
  CHECK(panel.has_cached_data());

  // 在途换库：join + 清场，不采纳陈旧批次
  panel.start_run(t.session);
  panel.on_database_closed();
  CHECK_FALSE(panel.run_in_flight());
  panel.poll();
  CHECK_FALSE(panel.has_cached_data());
  CHECK_FALSE(state.status_is_error);  // 清场不写状态栏
}

TEST_CASE("面板状态机：换库清理序列（复核 2.13 + 第三轮新-C5）") {
  TempDb t;
  std::string why;
  REQUIRE(t.open(&why));
  auto made = t.session.persons().create_person(
      make_person("陈", "始祖", Gender::Male, "1600"));
  REQUIRE(made.has_value());

  AppState state;
  state.selected_person_id = made->id;
  state.relation_ego = made->id;
  state.relation_alter = made->id;

  // 六面板齐全：缓存可填充的三个填满，其余保持空（序列不得引入缓存）
  PersonCardPanel card(state);
  TreePanel tree(state);
  ValidationPanel validation(state);
  SearchPanel search(state);
  RelationPanel relation(state);
  PersonTablePanel table(state);

  state.selected_person_id = made->id;
  REQUIRE(card.load(t.session));
  tree.begin_load(t.session, made->id, 10);
  wait_until([&] { return !tree.load_in_flight(); });
  tree.poll();
  validation.start_run(t.session);
  wait_until([&] { return !validation.run_in_flight(); });
  validation.poll();

  CHECK(card.has_cached_data());
  CHECK(tree.has_cached_data());
  CHECK(validation.has_cached_data());

  std::vector<Panel*> panels = {&card, &tree, &validation,
                                &search, &relation, &table};
  reset_for_database_switch(state, panels);

  // 全局选中态清空（第三轮新-C5：关系端点一并清）
  CHECK(state.selected_person_id.empty());
  CHECK(state.relation_ego.empty());
  CHECK(state.relation_alter.empty());
  // 六面板缓存全清——换库后检索/关系/校验/人物卡/树不再展示旧库数据
  for (const auto* p : panels) {
    CHECK_FALSE(p->has_cached_data());
  }
}
