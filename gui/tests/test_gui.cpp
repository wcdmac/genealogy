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
#include <genealogy/gui/panels/export_panel.hpp>
#include <genealogy/gui/svg_preview.hpp>
#include <genealogy/gui/panels/branch_panel.hpp>
#include <genealogy/gui/panels/place_panel.hpp>
#include <genealogy/gui/panels/poem_panel.hpp>
#include <genealogy/gui/panels/backup_panel.hpp>
#include <genealogy/gui/panels/timeline_panel.hpp>
#include <genealogy/gui/panels/attachment_panel.hpp>
#include <genealogy/gui/panels/merge_panel.hpp>
#include <genealogy/util/sha256.hpp>
#include <genealogy/gui/panels/new_person_panel.hpp>
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

TEST_CASE("新建人物面板：智能预填与父子关系创建") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  // 先建焦点人物（男，陈某，世代 5）
  Person focus = make_person("陈", "大山", Gender::Male, "1950");
  focus.generation_no = 5;
  auto f = db.session.persons().create_person(focus);
  REQUIRE(f.has_value());

  AppState state;
  state.selected_person_id = f->id;
  NewPersonPanel panel(state);
  panel.prefill(db.session, f->id);
  CHECK(panel.draft().family_name == "陈");
  CHECK(panel.draft().generation_no == "6");
  CHECK(panel.draft().father_id == f->id);

  // 创建儿子（老谱原文日期）
  panel.draft().given_name = "小川";
  panel.draft().birth_date = "同治三年四月初四";
  REQUIRE(panel.create(db.session));
  CHECK(state.selected_person_id != f->id);  // 新焦点
  auto son = db.session.persons().get_person(state.selected_person_id);
  REQUIRE(son.has_value());
  CHECK(son->family_name == "陈");
  CHECK(son->father_id == f->id);
  CHECK(son->generation_no.value_or(0) == 6);
  CHECK(son->birth_date.lunar.value_or("").find("同治") != std::string::npos);
  // 创建后一次性字段清空、预填保留
  CHECK(panel.draft().given_name.empty());
  CHECK(panel.draft().family_name == "陈");
}

TEST_CASE("新建人物面板：空名拒绝、坏日期报错") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  AppState state;
  NewPersonPanel panel(state);
  CHECK_FALSE(panel.create(db.session));  // 空名
  CHECK(state.status_is_error);
  panel.draft().given_name = "某";
  panel.draft().birth_date = "不存在的年月";
  // from_raw 照录不校验，故原文任何内容都可存——改测 ISO 非法
  panel.draft().birth_date = "9999-99-99";
  CHECK_FALSE(panel.create(db.session));
  CHECK(state.status_is_error);
}

TEST_CASE("排版导出面板：三种产出均可生成") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  Person p = make_person("林", "太祖", Gender::Male, "1900");
  auto created = db.session.persons().create_person(p);
  REQUIRE(created.has_value());

  AppState state;
  state.selected_person_id = created->id;
  ExportPanel panel(state);

  SECTION("古籍版式：标记文本 → SVG") {
    std::string markup = panel.build_markup(db.session, created->id);
    CHECK(markup.find("〔卷〕") != std::string::npos);
    CHECK(markup.find("林太祖") != std::string::npos);
    std::string svg, err;
    REQUIRE(panel.generate_layout(markup, "guji_classic", "", "林氏家谱",
                                  &svg, &err));
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("林氏家谱") != std::string::npos);
    // 字号覆盖生效
    std::string svg2;
    REQUIRE(panel.generate_layout(markup, "guji_classic", "22pt", "林氏",
                                  &svg2, &err));
    CHECK(svg2.find("font-size:29px") != std::string::npos);
  }
  SECTION("世系图：焦点 → SVG") {
    std::string svg, err;
    REQUIRE(panel.generate_chart(db.session, "descendant", created->id, 5,
                                 &svg, &err));
    CHECK(svg.find("<svg") != std::string::npos);
  }
  SECTION("谱书：人物 → HTML") {
    std::string html, err;
    REQUIRE(panel.generate_book(db.session, &html, &err));
    bool has_html = html.find("<html") != std::string::npos ||
                    html.find("<!DOCTYPE") != std::string::npos;
    CHECK(has_html);
  }
}

TEST_CASE("关系快捷：子女与配偶真正建立关系") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  auto parent = db.session.persons().create_person(
      make_person("赵", "父", Gender::Male, "1950"));
  REQUIRE(parent.has_value());

  AppState state;
  state.selected_person_id = parent->id;
  NewPersonPanel panel(state);

  SECTION("+子女：新建儿子并自动填 father_id") {
    state.new_relation_kind = "child";
    state.new_relation_anchor = parent->id;
    panel.prefill(db.session, parent->id);
    CHECK(panel.draft().father_id == parent->id);
    panel.draft().given_name = "儿";
    REQUIRE(panel.create(db.session));
    auto son = db.session.persons().get_person(state.selected_person_id);
    REQUIRE(son.has_value());
    CHECK(son->father_id == parent->id);
  }

  SECTION("+配偶：新建女性并建立婚姻 Union") {
    state.new_relation_kind = "spouse";
    state.new_relation_anchor = parent->id;
    panel.prefill(db.session, parent->id);
    panel.draft().given_name = "母";
    panel.draft().gender = 1;  // 女
    REQUIRE(panel.create(db.session));
    UnionFilter uf;
    auto unions = db.session.unions().list_unions(uf);
    REQUIRE(unions.has_value());
    CHECK(unions->size() == 1);
    bool linked = false;
    for (const auto& m : (*unions)[0].members)
      if (m.person_id == state.selected_person_id) linked = true;
    CHECK(linked);
  }

  SECTION("+父亲：新建父并回填锚点 father_id") {
    state.new_relation_kind = "father";
    state.new_relation_anchor = parent->id;
    panel.prefill(db.session, parent->id);
    panel.draft().given_name = "祖父";
    REQUIRE(panel.create(db.session));
    auto updated = db.session.persons().get_person(parent->id);
    REQUIRE(updated.has_value());
    CHECK(updated->father_id == state.selected_person_id);
  }
}

TEST_CASE("SVG 预览：解析自家 SVG 子集（无光栅化依赖）") {
  AppState st;
  ExportPanel panel(st);
  std::string markup = "〔卷〕陈氏族谱\n〔章〕世家\n正文〔注〕小注〔/注〕〔圈〕";
  std::string svg, why;
  REQUIRE(panel.generate_layout(markup, "guji_classic", "", "陈氏", &svg, &why));
  panel.set_preview(svg);
  REQUIRE(panel.refresh_scene(&why));
  const auto& scene = panel.scene();
  // 图元：背景 rect + 版框 + 界行 + 鱼尾/象鼻 + 文字 等
  CHECK((svg_node_count(scene) > 20));
  CHECK((svg_class_count(scene) >= 5));
  CHECK(scene.width == 720);
  CHECK(scene.height == 1040);
  // class 样式来自 <style>，非硬编码
  auto it = scene.classes.find("body");
  REQUIRE(it != scene.classes.end());
  CHECK(it->second.font_size == 15);
  CHECK(it->second.font_size == 15);
  // 有文字节点且内容被正确反转义（含中文）
  bool has_text = false;
  for (const auto& n : scene.nodes) {
    if (n.kind == genealogy::gui::SvgNode::Kind::Text && !n.text.empty()) {
      has_text = true;
      break;
    }
  }
  CHECK(has_text);
  // 有 path（专名线/鱼尾）
  bool has_path = false;
  for (const auto& n : scene.nodes)
    if (n.kind == genealogy::gui::SvgNode::Kind::Path) has_path = true;
  CHECK(has_path);
}

TEST_CASE("支派面板：新建与更新（含乐观锁）") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  AppState state;
  BranchPanel panel(state);
  REQUIRE(panel.reload(db.session, &why));
  CHECK(panel.rows().empty());

  panel.new_draft();
  panel.edit_name("长房");
  panel.sync_draft();
  REQUIRE(panel.save(db.session, &why));
  CHECK(panel.rows().size() == 1);
  CHECK_FALSE(panel.draft().id.empty());  // 新建分配了 id
  const auto created_id = panel.draft().id;

  // 选中并更新
  panel.select(panel.rows()[0]);
  CHECK(panel.editing());
  panel.edit_note("始迁祖支");
  panel.sync_draft();
  REQUIRE(panel.save(db.session, &why));
  CHECK(panel.rows().size() == 1);  // 更新不新增
  CHECK(panel.rows()[0].id == created_id);
  CHECK(panel.rows()[0].note.value_or("") == "始迁祖支");

  // 空名拒绝
  panel.new_draft();
  CHECK_FALSE(panel.save(db.session, &why));
}

TEST_CASE("地点面板：新建、经纬度校验与沿革") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  AppState state;
  PlacePanel panel(state);
  REQUIRE(panel.reload(db.session, &why));

  panel.new_draft();
  panel.edit_name("河南颍川");
  panel.edit_historical("颍川郡");
  panel.sync_draft();
  REQUIRE(panel.save(db.session, &why));
  CHECK(panel.rows().size() == 1);
  CHECK(panel.rows()[0].historical_name.value_or("") == "颍川郡");

  // 经纬度校验（通过状态机的缓冲入口无法直接设字符串，验证 save 的空值语义）
  panel.select(panel.rows()[0]);
  panel.edit_lat("34.2");
  panel.edit_lng("113.5");
  REQUIRE(panel.save(db.session, &why));
  CHECK(panel.rows()[0].lat.value_or(0) > 34);
}


TEST_CASE("字辈面板：谱级派语录入与读取") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  AppState state;
  PoemPanel panel(state);
  panel.edit_text("天地玄黄，宇宙洪荒");
  REQUIRE(panel.set(db.session, &why));
  CHECK(panel.entries().size() == 8);  // 引擎只取汉字
  CHECK(panel.entries()[0].character == "天");
  CHECK(panel.entries()[7].character == "荒");

  // 清空缓冲再读取，应从服务拿回
  panel.clear();
  REQUIRE(panel.reload(db.session, &why));
  CHECK(panel.entries().size() == 8);
  CHECK(panel.text_buf().find("天") != std::string::npos);

  // 一致性校验（空库无冲突）
  REQUIRE(panel.check(db.session, &why));
  CHECK(panel.conflicts().empty());
}


TEST_CASE("世系表：行内编辑走 rev 乐观锁") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  auto made = db.session.persons().create_person(
      make_person("钱", "三", Gender::Male, "1980"));
  REQUIRE(made.has_value());

  AppState state;
  state.db_path = db.path.string();
  PersonTablePanel panel(state);
  panel.reload_for_test(db.session);  // 载入 rows_

  std::string err;
  REQUIRE(panel.begin_edit(made->id, &err));
  CHECK(panel.editing());
  CHECK(panel.edit_given() == "三");

  panel.edit_given("四");
  panel.set_edit_gender(1);
  REQUIRE(panel.save_edit(db.session, &err));
  CHECK_FALSE(panel.editing());  // 保存后退出编辑态
  auto updated = db.session.persons().get_person(made->id);
  REQUIRE(updated.has_value());
  CHECK(updated->given_name == "四");
  CHECK(updated->gender == Gender::Female);

  // 空名拒绝
  REQUIRE(panel.begin_edit(made->id, &err));
  panel.edit_given("");
  CHECK_FALSE(panel.save_edit(db.session, &err));
}


TEST_CASE("备份面板：快照、拒绝覆盖、force 恢复") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  auto made = db.session.persons().create_person(
      make_person("孙", "备份", Gender::Male, "1970"));
  REQUIRE(made.has_value());

  AppState state;
  BackupPanel panel(state);
  const auto dir = (std::filesystem::temp_directory_path() /
                    ("genealogy-bk-" + generate_ulid())).string();
  std::string snap, moved;
  REQUIRE(panel.backup(db.session, dir, &snap, &why));
  CHECK(std::filesystem::exists(snap));

  // 目标已存在、未 force → 拒绝
  CHECK_FALSE(panel.restore(snap, db.path.string(), false, &moved, &why));
  CHECK(why.find("已存在") != std::string::npos);

  // force → 旧库移开为 .bak，恢复成功
  REQUIRE(panel.restore_and_reopen(db.session, snap, true, &moved, &why));
  CHECK(moved.find(".bak-") != std::string::npos);
  // 恢复后重新打开，数据在
  DbSession reopened;
  REQUIRE(reopened.open(db.path.string(), &why));
  auto got = reopened.persons().get_person(made->id);
  CHECK(got.has_value());

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}


TEST_CASE("时间线面板：事件排序与地点统计") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  auto person = db.session.persons().create_person(
      make_person("周", "事件", Gender::Male, "1900"));
  REQUIRE(person.has_value());

  auto place = db.session.places().create_place([] {
    Place p; p.name = "河南"; return p; }());
  REQUIRE(place.has_value());

  // 两条事件：1920 出生、1950 卒（均关联地点）
  for (const char* iso : {"1920-03-15", "1950-08-20"}) {
    Event e;
    e.subject_type = EventSubjectType::Person;
    e.subject_id = person->id;
    e.type = EventType::Birth;
    auto parsed = GenealogyDate::from_iso(iso);
    REQUIRE(parsed.has_value());
    e.date = *parsed;
    e.place_id = place->id;
    auto made = db.session.events().create_event(e);
    REQUIRE(made.has_value());
  }

  AppState state;
  TimelinePanel panel(state);
  REQUIRE(panel.reload(db.session, &why));
  REQUIRE(panel.rows().size() == 2);
  CHECK(panel.rows()[0].year == "1920-03-15");  // 按年份升序
  CHECK(panel.rows()[1].year == "1950-08-20");
  CHECK(panel.rows()[0].subject == "周事件");
  CHECK(panel.rows()[0].place == "河南");
  REQUIRE(panel.places().size() == 1);
  CHECK(panel.places()[0].name == "河南");
  CHECK(panel.places()[0].count == 2);

  // 类型筛选：迁徙应无结果
  panel.set_type_filter(5);
  REQUIRE(panel.reload(db.session, &why));
  CHECK(panel.rows().empty());
}


TEST_CASE("SHA-256 已知向量") {
  // NIST FIPS 180-4 标准向量
  CHECK(sha256_hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256_hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  // 跨 64 字节边界
  CHECK(sha256_hex(std::string(64, 'a')).size() == 64);
  CHECK(sha256_hex(std::string(65, 'a')) != sha256_hex(std::string(64, 'a')));
}

TEST_CASE("附件面板：导入、sha256 去重与列表") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));
  auto person = db.session.persons().create_person(
      make_person("吴", "附件", Gender::Male, "1980"));
  REQUIRE(person.has_value());

  const auto dir = std::filesystem::temp_directory_path() /
                   ("genealogy-att-" + generate_ulid());
  std::filesystem::create_directories(dir);
  const auto file = dir / "portrait.txt";
  { std::ofstream out(file, std::ios::binary); out << "portrait-bytes"; }

  AppState state;
  AttachmentPanel panel(state);
  REQUIRE(panel.import_file(db.session, file.string(),
                            AttachmentKind::Portrait, person->id,
                            "遗像", &why));
  REQUIRE(panel.rows().size() == 1);
  CHECK(panel.rows()[0].sha256 == sha256_hex("portrait-bytes"));
  CHECK(panel.rows()[0].caption.value_or("") == "遗像");
  // 内容文件已落盘
  const auto stored = db.session.attachments().content_path(
      panel.rows()[0].sha256);
  CHECK(std::filesystem::exists(stored));

  // 再次导入同一文件 → 两条元数据但共享同一内容文件（sha256 去重）
  REQUIRE(panel.import_file(db.session, file.string(),
                            AttachmentKind::Photo, person->id, "", &why));
  CHECK(panel.rows().size() == 2);
  CHECK(panel.rows()[0].sha256 == panel.rows()[1].sha256);

  // 删除一条
  REQUIRE(panel.remove(db.session, panel.rows()[0].id, &why));
  CHECK(panel.rows().size() == 1);
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}


TEST_CASE("重复合并：检测四级建议、冲突拒绝、关系迁移") {
  TempDb db;
  std::string why;
  REQUIRE(db.open(&why));

  // 一对同名同出生年（强烈建议）
  auto a = db.session.persons().create_person(
      make_person("郑", "重复", Gender::Male, "1900"));
  REQUIRE(a.has_value());
  auto b = db.session.persons().create_person(
      make_person("郑", "重复", Gender::Male, "1900"));
  REQUIRE(b.has_value());
  // 一个同名但出生年不同（不建议）
  auto c = db.session.persons().create_person(
      make_person("郑", "重复", Gender::Male, "1910"));
  REQUIRE(c.has_value());

  AppState state;
  MergePanel panel(state);
  REQUIRE(panel.scan(db.session, &why));
  CHECK(panel.pairs().size() == 3);  // a-b, a-c, b-c
  bool has_strong = false, has_unlikely = false;
  for (const auto& p : panel.pairs()) {
    if (p.confidence == genealogy::MergeConfidence::Strong) has_strong = true;
    if (p.confidence == genealogy::MergeConfidence::Unlikely) has_unlikely = true;
  }
  CHECK(has_strong);
  CHECK(has_unlikely);

  // 预览无冲突（a 与 b 字段一致）
  REQUIRE(panel.preview(db.session, a->id, b->id, &why));
  CHECK(panel.conflicts().empty());

  // 给 a、b 各设不同父亲 → 冲突，合并应拒绝
  auto father1 = db.session.persons().create_person(
      make_person("郑", "父甲", Gender::Male, "1870"));
  auto father2 = db.session.persons().create_person(
      make_person("郑", "父乙", Gender::Male, "1871"));
  REQUIRE(father1.has_value());
  REQUIRE(father2.has_value());
  auto winner = db.session.persons().get_person(a->id);
  REQUIRE(winner.has_value());
  winner->father_id = father1->id;
  REQUIRE(db.session.persons().update_person(*winner).has_value());
  auto loser = db.session.persons().get_person(b->id);
  REQUIRE(loser.has_value());
  loser->father_id = father2->id;
  REQUIRE(db.session.persons().update_person(*loser).has_value());
  REQUIRE(panel.preview(db.session, a->id, b->id, &why));
  CHECK_FALSE(panel.conflicts().empty());
  CHECK(panel.conflicts()[0].field == "父亲");

  // 清掉冲突后合并：关系迁移 + 快照
  loser = db.session.persons().get_person(b->id);
  REQUIRE(loser.has_value());
  loser->father_id = father1->id;  // 与 winner 一致 → 无冲突
  REQUIRE(db.session.persons().update_person(*loser).has_value());
  // 给 loser 一个事件，验证迁移
  Event e;
  e.subject_type = genealogy::EventSubjectType::Person;
  e.subject_id = b->id;
  e.type = genealogy::EventType::Birth;
  auto ev = db.session.events().create_event(e);
  REQUIRE(ev.has_value());

  const auto dir = (std::filesystem::temp_directory_path() /
                    ("genealogy-merge-" + generate_ulid())).string();
  genealogy::MergeService svc(db.session.db());
  auto result = svc.merge(a->id, b->id, false, dir);
  REQUIRE(result.has_value());
  CHECK_FALSE(result->backup_path.empty());
  CHECK(std::filesystem::exists(result->backup_path));
  CHECK_FALSE(db.session.persons().get_person(b->id).has_value());  // loser 已删
  genealogy::EventFilter ef;
  ef.subject_id = a->id;
  auto migrated = db.session.events().list_events(ef);
  REQUIRE(migrated.has_value());
  CHECK(migrated->size() == 1);  // 事件迁到 winner
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
