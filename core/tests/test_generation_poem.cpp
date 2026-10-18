// 字辈引擎测试（M4）。
//
// 覆盖三层：
//   1. 引擎纯函数（联句拆分 / 辈字定位 / 惯例推断 / 交叉校验）——快照式断言；
//   2. 仓储（整份替换的事务语义）；
//   3. 用例层（录入 → 查询 → 校验全链路 + 归属不存在时报错）。
//
// 用真实派语做夹具：「德宗景文正，邦家万世兴」是常见十句派语形态。

#include <catch2/catch_test_macros.hpp>

#include <genealogy/lineage/generation.hpp>
#include <genealogy/lineage/generation_poem.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/generation_poem.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/generation_poem_repository.hpp>
#include <genealogy/service/generation_poem_service.hpp>
#include <genealogy/ulid.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace genealogy;
using namespace genealogy::lineage;

namespace {

constexpr const char* kPoemText = "德宗景文正，邦家万世兴";

std::vector<std::string> fixture_poem() { return parse_poem(kPoemText); }

}  // namespace

// ---------------------------------------------------------------- 引擎

TEST_CASE("parse_poem：联句取汉字、弃标点") {
  const auto p = parse_poem(kPoemText);
  REQUIRE(p.size() == 10);
  CHECK(p[0] == "德");
  CHECK(p[1] == "宗");
  CHECK(p[4] == "正");
  CHECK(p[9] == "兴");
}

TEST_CASE("parse_poem：杂串鲁棒性") {
  // 空白/数字/英文/标点一概忽略；连续汉字全收
  const auto p = parse_poem("  A1 德、宗! 景?? 文  ");
  REQUIRE(p.size() == 4);
  CHECK(p[0] == "德");
  CHECK(p[3] == "文");

  // 空串与纯标点 → 空
  CHECK(parse_poem("").empty());
  CHECK(parse_poem("，。！１２３ abc").empty());
}

TEST_CASE("locate_in_name：命中位置与辈序") {
  const auto p = fixture_poem();
  // 「文远」：文在第 4 句、名字第 1 字
  const auto hits = locate_in_name(p, "文远");
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].position == 0);
  CHECK(hits[0].seq == 4);

  // 辈字在次字（双名次字冠辈的支派形态）：全部命中都要报
  const auto hits2 = locate_in_name(p, "世正");
  REQUIRE(hits2.size() == 2);
  CHECK(hits2[0].seq == 9);   // 世
  CHECK(hits2[1].seq == 5);   // 正
}

TEST_CASE("locate_in_name：同字在派语中多次出现时全部列出") {
  const std::vector<std::string> p = {"文", "世", "文", "兴"};
  const auto hits = locate_in_name(p, "文远");
  REQUIRE(hits.size() == 2);   // 第 1 句与第 3 句都是「文」
  CHECK(hits[0].seq == 1);
  CHECK(hits[1].seq == 3);
}

TEST_CASE("expected_seq：惯例取名字第一个字") {
  const auto p = fixture_poem();
  CHECK(expected_seq(p, "文远") == 4);      // 双名首字
  CHECK(expected_seq(p, "世正") == 9);      // 双名首字（次字的正也命中，但惯例取首字）
  CHECK(expected_seq(p, "兴") == 10);       // 单名本字
  CHECK(expected_seq(p, "无名") == std::nullopt);   // 无命中
  CHECK(expected_seq(p, "") == std::nullopt);
}

// ---------------------------------------------------------------- 交叉校验

TEST_CASE("check_poem_consistency：一致不报、冲突报、缺边不报") {
  // 三代单链：始祖(德,1世) → 宗(2世) → 景(3世)
  std::vector<Person> persons;
  auto add = [&](const std::string& given, const std::string& father) {
    Person p;
    p.id = generate_ulid();
    p.lineage_id = kDefaultLineageId;
    p.family_name = "陈";
    p.given_name = given;
    p.gender = Gender::Male;
    if (!father.empty()) p.father_id = father;
    persons.push_back(p);
    return p.id;
  };
  const auto g1 = add("德盛", "");
  const auto g2 = add("宗业", g1);
  const auto g3 = add("景文", g2);   // 辈字「景」= 第 3 句，世代也是 3：一致
  (void)g3;

  const PersonGraph graph_ok = PersonGraph::build(persons, std::vector<Union>{});
  const auto gens = compute_generations(graph_ok);
  REQUIRE(gens.size() == 3);

  const auto p = fixture_poem();
  const auto conflicts = check_poem_consistency(graph_ok, gens, p, 1);
  CHECK(conflicts.empty());   // 三代全部一致

  // 把第三代改成「正X」：辈字第 5 句 vs 世代 3 → 冲突
  Person bad = persons.back();
  bad.given_name = "正邦";
  persons.back() = bad;
  const PersonGraph graph_bad =
      PersonGraph::build(persons, std::vector<Union>{});
  const auto gens2 = compute_generations(graph_bad);
  const auto c2 = check_poem_consistency(graph_bad, gens2, p, 1);
  REQUIRE(c2.size() == 1);
  CHECK(c2[0].name_seq == 5);
  CHECK(c2[0].generation_no == 3);
  CHECK(c2[0].expected_no == 5);
  CHECK(c2[0].detail.find("第 5 句") != std::string::npos);
}

TEST_CASE("check_poem_consistency：单边缺失不判冲突") {
  // 一个没辈字的人 + 一个世代断链（父母双缺非始祖）的人：都不是冲突
  std::vector<Person> persons;
  Person founder;
  founder.id = generate_ulid();
  founder.lineage_id = kDefaultLineageId;
  founder.family_name = "陈";
  founder.given_name = "德盛";
  persons.push_back(founder);

  Person no_char;
  no_char.id = generate_ulid();
  no_char.lineage_id = kDefaultLineageId;
  no_char.family_name = "陈";
  no_char.given_name = "阿福";   // 没有辈字
  persons.push_back(no_char);

  const PersonGraph graph = PersonGraph::build(persons, std::vector<Union>{});
  const auto gens = compute_generations(graph);
  const auto conflicts = check_poem_consistency(graph, gens, fixture_poem(), 1);
  CHECK(conflicts.empty());   // 宁缺勿滥：单边缺失不报
}

TEST_CASE("check_poem_consistency：poem_start 偏移生效") {
  // 支派从第 6 世起定派语：第 1 句应对应第 6 世
  std::vector<Person> persons;
  std::string father;
  for (int i = 0; i < 8; ++i) {
    Person p;
    p.id = generate_ulid();
    p.lineage_id = kDefaultLineageId;
    p.family_name = "陈";
    // 前 5 世随便取名，第 6 世起按派语（德宗景文正…）
    static const char* kNames[] = {"大", "二", "三", "四", "五",
                                   "德X", "宗X", "景X"};
    p.given_name = kNames[i];
    p.gender = Gender::Male;
    if (!father.empty()) p.father_id = father;
    father = p.id;
    persons.push_back(p);
  }
  const PersonGraph graph = PersonGraph::build(persons, std::vector<Union>{});
  const auto gens = compute_generations(graph);
  const auto p = fixture_poem();

  // 按 poem_start=6：第 6 世的人辈字应在第 1 句
  const auto ok = check_poem_consistency(graph, gens, p, 6);
  CHECK(ok.empty());
  // 按 poem_start=1（忘了偏移）：第 6/7/8 世全报冲突
  const auto bad = check_poem_consistency(graph, gens, p, 1);
  CHECK(bad.size() == 3);
}

// ---------------------------------------------------------------- 仓储

TEST_CASE("GenerationPoemRepository：整份替换的事务语义") {
  const auto path = std::filesystem::temp_directory_path() /
                    ("genealogy-poem-" + generate_ulid() + ".db");
  auto opened = Database::open(path);
  REQUIRE(opened.has_value());
  Database db = std::move(*opened);
  GenerationPoemRepository repo(db);

  const std::string owner = generate_ulid();
  std::vector<GenerationPoemEntry> first;
  for (int i = 0; i < 3; ++i) {
    GenerationPoemEntry e;
    e.owner_type = PoemOwner::Lineage;
    e.owner_id = owner;
    e.seq = i + 1;
    e.character = std::string(1, 'A' + static_cast<char>(i));
    first.push_back(e);
  }
  REQUIRE(repo.replace_all(PoemOwner::Lineage, owner, first).has_value());

  auto got = repo.list(PoemOwner::Lineage, owner);
  REQUIRE(got.has_value());
  REQUIRE(got->size() == 3);
  CHECK((*got)[0].seq == 1);
  CHECK((*got)[2].character == "C");

  // 再换一份 5 条的：旧的 3 条必须全部消失（不能留半截）
  std::vector<GenerationPoemEntry> second;
  for (int i = 0; i < 5; ++i) {
    GenerationPoemEntry e;
    e.owner_type = PoemOwner::Lineage;
    e.owner_id = owner;
    e.seq = i + 1;
    e.character = "字";
    second.push_back(e);
  }
  REQUIRE(repo.replace_all(PoemOwner::Lineage, owner, second).has_value());
  auto got2 = repo.list(PoemOwner::Lineage, owner);
  REQUIRE(got2.has_value());
  CHECK(got2->size() == 5);

  // 清空
  REQUIRE(repo.replace_all(PoemOwner::Lineage, owner, {}).has_value());
  auto got3 = repo.list(PoemOwner::Lineage, owner);
  REQUIRE(got3.has_value());
  CHECK(got3->empty());

  // 别的归属不受影响（owner_type/owner_id 双维度隔离）
  REQUIRE(repo.replace_all(PoemOwner::Branch, generate_ulid(), first)
              .has_value());
  auto got4 = repo.list(PoemOwner::Lineage, owner);
  REQUIRE(got4.has_value());
  CHECK(got4->empty());

  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + "-wal", ec);
  std::filesystem::remove(path.string() + "-shm", ec);
}

// ---------------------------------------------------------------- 用例层

TEST_CASE("GenerationPoemService：录入 → 查询 → 校验全链路") {
  const auto path = std::filesystem::temp_directory_path() /
                    ("genealogy-poem-svc-" + generate_ulid() + ".db");
  auto opened = Database::open(path);
  REQUIRE(opened.has_value());
  Database db = std::move(*opened);
  ensure_default_lineage(db);
  GenerationPoemService svc(db);

  // 幽灵归属：必须 NotFound，不能把派语挂到空气上
  auto ghost = svc.set_poem(PoemOwner::Lineage, generate_ulid(), kPoemText);
  CHECK_FALSE(ghost.has_value());
  CHECK(ghost.error().code() == ErrorCode::NotFound);

  // 默认宗族录入
  auto set = svc.set_poem(PoemOwner::Lineage, kDefaultLineageId, kPoemText);
  REQUIRE(set.has_value());
  REQUIRE(set->size() == 10);
  CHECK((*set)[0].seq == 1);
  CHECK((*set)[0].character == "德");

  // 原文留档到 lineage.generation_poem
  LineageRepository lineages(db);
  auto l = lineages.get(kDefaultLineageId);
  REQUIRE(l.has_value());
  REQUIRE(l->generation_poem.has_value());
  CHECK(*l->generation_poem == kPoemText);

  // 查询
  auto got = svc.get_poem(PoemOwner::Lineage, kDefaultLineageId);
  REQUIRE(got.has_value());
  CHECK(got->size() == 10);

  // 没建人物时校验：空图 → 无冲突（不是错误）
  auto check = svc.check_consistency(PoemOwner::Lineage, kDefaultLineageId);
  REQUIRE(check.has_value());
  CHECK(check->empty());
}

TEST_CASE("GenerationPoemService：空联句与未录入的校验") {
  const auto path = std::filesystem::temp_directory_path() /
                    ("genealogy-poem-svc2-" + generate_ulid() + ".db");
  auto opened = Database::open(path);
  REQUIRE(opened.has_value());
  Database db = std::move(*opened);
  ensure_default_lineage(db);
  GenerationPoemService svc(db);

  // 纯标点联句：invalid_argument
  auto bad = svc.set_poem(PoemOwner::Lineage, kDefaultLineageId, "，。！！");
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error().code() == ErrorCode::InvalidArgument);

  // 没录入派语就校验：说清楚「无从校验」，不是返回空列表冒充通过
  auto check = svc.check_consistency(PoemOwner::Lineage, kDefaultLineageId);
  CHECK_FALSE(check.has_value());
  CHECK(check.error().code() == ErrorCode::InvalidArgument);

  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(path.string() + "-wal", ec);
  std::filesystem::remove(path.string() + "-shm", ec);
}

// ---------------------------------------------------------------- 模型键

TEST_CASE("poem_owner_key：归属键稳定可读") {
  CHECK(poem_owner_key(PoemOwner::Lineage, "01ABC") == "lineage:01ABC");
  CHECK(poem_owner_key(PoemOwner::Branch, "01XYZ") == "branch:01XYZ");
  // 序列化往返
  GenerationPoemEntry e;
  e.owner_type = PoemOwner::Branch;
  e.owner_id = "01XYZ";
  e.seq = 7;
  e.character = "德";
  const auto j = to_json(e);
  CHECK(j["owner_type"] == "branch");
  CHECK(j["seq"] == 7);
  CHECK(j["char"] == "德");
}
