// M1.5 导入导出的数据库集成测试：走真实的用例层落库，
// 再把库里的数据导出来、二次导入，验证「文件 → 库 → 文件 → 库」闭环
// 与 CLI 的 io import / io export 是同一条路径。

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/io/gedcom.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/lineage_service.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/union_service.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-io-" + generate_ulid() + ".db");
}

struct DbGuard {
  std::filesystem::path p;
  ~DbGuard() {
    std::error_code ec;
    std::filesystem::remove(p, ec);
    std::filesystem::remove(p.string() + "-wal", ec);
    std::filesystem::remove(p.string() + "-shm", ec);
  }
};

const char* kGed = R"GED(0 HEAD
1 SOUR 集成测试谱
2 VERS 1.0
1 CHAR UTF-8
0 @I1@ INDI
1 NAME 文远 /陈/
1 SEX M
1 BIRT
2 DATE 9 MAY 1864
2 PLAC 湖南湘潭
1 FAMS @F1@
0 @I2@ INDI
1 NAME 门查氏 /王/
1 SEX F
1 BIRT
2 DATE ABT 1866
1 FAMS @F1@
0 @I3@ INDI
1 NAME 承嗣 /陈/
1 SEX M
1 BIRT
2 DATE 8 AUG 1888
1 FAMC @F1@
0 @F1@ FAM
1 HUSB @I1@
1 WIFE @I2@
1 CHIL @I3@
1 MARR
2 DATE 1883
0 TRLR
)GED";

}  // namespace

TEST_CASE("GEDCOM 集成：文件 → 库 → 文件 → 库 闭环，回流零损失") {
  DbGuard guard{temp_db_path()};
  auto db = Database::open(guard.p.string());
  REQUIRE(db.has_value());
  ensure_default_lineage(*db);

  LineageService lineages(*db);
  Lineage lineage;
  lineage.name = "集成谱";
  lineage.surname = "陈";
  auto made_lineage = lineages.create_lineage(lineage);
  REQUIRE(made_lineage.has_value());
  const std::string lid = made_lineage->id;

  auto doc = io::parse_gedcom(kGed);
  REQUIRE(doc.has_value());

  // ---- 第一轮导入 ----
  io::ImportOptions opt;
  opt.lineage_id = lid;
  opt.default_surname = "陈";
  auto first = io::import_gedcom(*doc, opt);
  REQUIRE(first.has_value());

  PlaceService places(*db);
  PersonService persons(*db);
  UnionService unions(*db);
  EventService events(*db);

  // 落库顺序：地点 → 人物（父母先于子女）→ 结合 → 事件。
  // 地点必须最先：person/union/event 都有 place_id 外键。
  for (const auto& pl : first->places) {
    auto made = places.create_place(pl);
    REQUIRE(made.has_value());
  }
  // 依赖排序：父母先于子女（GEDCOM 里子记录可以排在父记录前）
  for (const auto& p : io::order_by_dependency(first->persons)) {
    auto made = persons.create_person(p);
    if (!made.has_value()) {
      INFO("建档 " << p.family_name << p.given_name
                   << " 失败: " << made.error().to_string());
    }
    REQUIRE(made.has_value());
  }
  for (const auto& u : first->unions) {
    auto made = unions.create_union(u);
    REQUIRE(made.has_value());
  }
  for (const auto& e : first->events) {
    auto made = events.create_event(e);
    REQUIRE(made.has_value());
  }
  REQUIRE(first->persons.size() == 3);
  REQUIRE(first->unions.size() == 1);

  // ---- 导出 ----
  PersonFilter pf;
  pf.limit = 1000;
  auto listed = persons.list_persons(pf);
  REQUIRE(listed.has_value());
  std::vector<Person> rows;
  for (auto& p : *listed) {
    if (p.lineage_id == lid) rows.push_back(p);
  }
  REQUIRE(rows.size() == 3);

  UnionFilter uf;
  uf.limit = 1000;
  auto union_list = unions.list_unions(uf);
  REQUIRE(union_list.has_value());
  std::vector<Union> union_rows = *union_list;

  EventFilter ef;
  ef.limit = 1000;
  auto event_list = events.list_events(ef);
  REQUIRE(event_list.has_value());
  std::vector<Event> event_rows = *event_list;

  PlaceFilter plf;
  plf.limit = 1000;
  auto place_list = places.list_places(plf);
  REQUIRE(place_list.has_value());
  std::vector<Place> place_rows = *place_list;

  io::ExportOptions eo;
  eo.persons = &rows;
  eo.unions = &union_rows;
  eo.events = &event_rows;
  eo.places = &place_rows;
  const std::string text = io::export_gedcom(eo);

  // ---- 第二轮导入（新库）----
  DbGuard guard2{temp_db_path()};
  auto db2 = Database::open(guard2.p.string());
  REQUIRE(db2.has_value());
  ensure_default_lineage(*db2);
  LineageService lineages2(*db2);
  Lineage l2;
  l2.name = "回流谱";
  l2.surname = "陈";
  auto made2 = lineages2.create_lineage(l2);
  REQUIRE(made2.has_value());

  auto doc2 = io::parse_gedcom(text);
  REQUIRE(doc2.has_value());
  io::ImportOptions opt2;
  opt2.lineage_id = made2->id;
  opt2.default_surname = "陈";
  auto second = io::import_gedcom(*doc2, opt2);
  REQUIRE(second.has_value());

  // 回流不该有任何 Warning；唯一的 Info 是 DIV（本例没有离异，应为空）
  for (const auto& l : second->losses) {
    INFO("loss: " << l.tag << " " << l.reason);
    CHECK(l.severity == io::LossSeverity::Info);
  }
  CHECK(second->losses.empty());
  CHECK(second->persons.size() == 3);
  CHECK(second->unions.size() == 1);

  // 姓名、性别、日期一一对上
  auto find = [](const std::vector<Person>& v,
                 const std::string& given) -> const Person* {
    for (const auto& p : v) {
      if (p.given_name == given) return &p;
    }
    return nullptr;
  };
  const Person* w2 = find(second->persons, "文远");
  const Person* m2 = find(second->persons, "门查氏");
  const Person* c2 = find(second->persons, "承嗣");
  REQUIRE(w2 != nullptr);
  REQUIRE(m2 != nullptr);
  REQUIRE(c2 != nullptr);
  CHECK(w2->family_name == "陈");
  CHECK(w2->gender == Gender::Male);
  CHECK(w2->birth_date.gregorian.value() == "1864-05-09");
  CHECK(m2->family_name == "王");
  CHECK(m2->gender == Gender::Female);
  CHECK(m2->birth_date.qualifier.value() == "约");   // ABT → 约
  CHECK(c2->birth_date.gregorian.value() == "1888-08-08");
  // 父母链
  REQUIRE(c2->father_id.has_value());
  REQUIRE(c2->mother_id.has_value());
  CHECK(*c2->father_id == w2->id);
  CHECK(*c2->mother_id == m2->id);
  // 婚姻
  REQUIRE(second->unions.size() == 1);
  const Union& u2 = second->unions.front();
  CHECK(u2.members.size() == 2);
  CHECK(u2.start_date.has_value());
  CHECK(u2.start_date->gregorian.value() == "1883");
}
