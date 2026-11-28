// M1.3 世系算法测试：图遍历、世代计算、关系路径与规则键、
// 五服快照（明清定型版）、称谓快照、兜底与关系环。

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/lineage/appellation.hpp>
#include <genealogy/lineage/generation.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/mourning.hpp>
#include <genealogy/lineage/relationship.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/union.hpp>

using namespace genealogy;
using namespace genealogy::lineage;

namespace {

// ---- 样板家（九族）--------------------------------------------------------
//
//   g4(高祖1780) — g3(曾祖1810) — g2(祖父1835) — f(父1845) + m(母1848)
//                                  ├ b1(伯父1838) + b1w
//                                  └ b2(叔父1855) + b2w
//   f+m: be(兄1870)+bew, ego(己1875)+w(妻1877), zy(妹1880)+zyh
//   b1+b1w: tb(堂兄1872), td(堂妹1882)      b2+b2w: tby(堂弟1885)
//   gz(姑母1842, f 之姊)+gzh: tbm(姑表兄1873)
//   mf(外祖父1820)+mm(外祖母1822): m, mj(舅1846)+mjw: tbmm(舅表妹1886)
//   gb(伯祖父1833)+gbw: tbf(堂伯1840)       zbg(曾伯祖父1808)+zbgw: zf(1842): zc(1876)
//   wf(岳父1850)+wm(岳母1852): w, wz(妻姐1875)+wzh(连襟1874)
//   be+bew: ns(侄1895), nd(侄女1897)        zy+zyh: ws(外甥1900)

Person mk(const std::string& id, Gender g, int year, const std::string& father,
          const std::string& mother, int birth_order = 0, bool heir = false) {
  Person p;
  p.id = id;
  p.gender = g;
  p.birth_date = GenealogyDate::from_iso(std::to_string(year)).value();
  if (!father.empty()) p.father_id = father;
  if (!mother.empty()) p.mother_id = mother;
  if (birth_order > 0) p.birth_order = birth_order;
  p.is_heir = heir;
  return p;
}

Union couple(const std::string& a, MemberRole ra, const std::string& b,
             MemberRole rb) {
  Union u;
  u.members.push_back(UnionMember{a, ra});
  u.members.push_back(UnionMember{b, rb});
  return u;
}

std::vector<Person> sample_persons(bool ego_is_heir = false,
                                   bool nephew_is_heir = false) {
  std::vector<Person> v;
  // 高祖—曾祖—祖父三系
  v.push_back(mk("g4", Gender::Male, 1780, "", ""));
  v.push_back(mk("g4w", Gender::Female, 1782, "", ""));
  v.push_back(mk("g3", Gender::Male, 1810, "g4", "g4w"));
  v.push_back(mk("g3w", Gender::Female, 1812, "", ""));
  v.push_back(mk("g2", Gender::Male, 1835, "g3", "g3w"));
  v.push_back(mk("g2w", Gender::Female, 1837, "", ""));
  // 父辈三房
  v.push_back(mk("b1", Gender::Male, 1838, "g2", "g2w", 1));
  v.push_back(mk("b1w", Gender::Female, 1840, "", ""));
  v.push_back(mk("f", Gender::Male, 1845, "g2", "g2w", 2));
  v.push_back(mk("m", Gender::Female, 1848, "mf", "mm", 2));
  v.push_back(mk("b2", Gender::Male, 1855, "g2", "g2w", 3));
  v.push_back(mk("b2w", Gender::Female, 1857, "", ""));
  // 父之姊妹（姑母）与母党
  v.push_back(mk("gz", Gender::Female, 1842, "g2", "g2w", 4));
  v.push_back(mk("gzh", Gender::Male, 1840, "", ""));
  v.push_back(mk("mf", Gender::Male, 1820, "", ""));
  v.push_back(mk("mm", Gender::Female, 1822, "", ""));
  v.push_back(mk("mj", Gender::Male, 1846, "mf", "mm", 1));
  v.push_back(mk("mjw", Gender::Female, 1848, "", ""));  // 再从尊属
  v.push_back(mk("gb", Gender::Male, 1833, "g3", "g3w", 1));
  v.push_back(mk("gbw", Gender::Female, 1835, "", ""));
  v.push_back(mk("tbf", Gender::Male, 1840, "gb", "gbw"));
  v.push_back(mk("zbg", Gender::Male, 1808, "g4", "g4w", 1));
  v.push_back(mk("zbgw", Gender::Female, 1810, "", ""));
  v.push_back(mk("zf", Gender::Male, 1842, "zbg", "zbgw"));
  v.push_back(mk("zc", Gender::Male, 1876, "zf", ""));
  // ego 一代
  v.push_back(mk("be", Gender::Male, 1870, "f", "m", 1));
  v.push_back(mk("bew", Gender::Female, 1872, "", ""));
  v.push_back(mk("ego", Gender::Male, 1875, "f", "m", 2, ego_is_heir));
  v.push_back(mk("w", Gender::Female, 1877, "wf", "wm"));
  v.push_back(mk("zy", Gender::Female, 1880, "f", "m", 3));
  v.push_back(mk("zyh", Gender::Male, 1878, "", ""));
  // 妻党
  v.push_back(mk("wf", Gender::Male, 1850, "", ""));
  v.push_back(mk("wm", Gender::Female, 1852, "", ""));
  v.push_back(mk("wz", Gender::Female, 1875, "wf", "wm", 1));
  v.push_back(mk("wzh", Gender::Male, 1874, "", ""));
  // 堂表兄弟姐妹
  v.push_back(mk("tb", Gender::Male, 1872, "b1", "b1w", 1));
  v.push_back(mk("td", Gender::Female, 1882, "b1", "b1w", 2));
  v.push_back(mk("tby", Gender::Male, 1885, "b2", "b2w", 1));
  v.push_back(mk("tbm", Gender::Male, 1873, "gz", "gzh", 1));
  v.push_back(mk("tbmm", Gender::Female, 1886, "mj", "mjw", 1));
  // 晚辈
  v.push_back(mk("ns", Gender::Male, 1895, "be", "bew", 1, nephew_is_heir));
  v.push_back(mk("nd", Gender::Female, 1897, "be", "bew", 2));
  v.push_back(mk("ws", Gender::Male, 1900, "zy", "zyh", 1));
  return v;
}

std::vector<Union> sample_unions() {
  std::vector<Union> v;
  v.push_back(couple("g4", MemberRole::Husband, "g4w", MemberRole::Wife));
  v.push_back(couple("g3", MemberRole::Husband, "g3w", MemberRole::Wife));
  v.push_back(couple("g2", MemberRole::Husband, "g2w", MemberRole::Wife));
  v.push_back(couple("b1", MemberRole::Husband, "b1w", MemberRole::Wife));
  v.push_back(couple("f", MemberRole::Husband, "m", MemberRole::Wife));
  v.push_back(couple("b2", MemberRole::Husband, "b2w", MemberRole::Wife));
  v.push_back(couple("gz", MemberRole::Wife, "gzh", MemberRole::Husband));
  v.push_back(couple("mf", MemberRole::Husband, "mm", MemberRole::Wife));
  v.push_back(couple("mj", MemberRole::Husband, "mjw", MemberRole::Wife));
  v.push_back(couple("gb", MemberRole::Husband, "gbw", MemberRole::Wife));
  v.push_back(couple("zbg", MemberRole::Husband, "zbgw", MemberRole::Wife));
  v.push_back(couple("be", MemberRole::Husband, "bew", MemberRole::Wife));
  v.push_back(couple("ego", MemberRole::Husband, "w", MemberRole::Wife));
  v.push_back(couple("zy", MemberRole::Wife, "zyh", MemberRole::Husband));
  v.push_back(couple("wf", MemberRole::Husband, "wm", MemberRole::Wife));
  v.push_back(couple("wz", MemberRole::Wife, "wzh", MemberRole::Husband));
  return v;
}

// 样例家：数据与图同生命周期（PersonGraph 持有指向 persons/unions 的
// 指针，临时 vector 会造成悬空——勿拆开使用）。
struct SampleFamily {
  std::vector<Person> persons;
  std::vector<Union> unions;
  PersonGraph graph;
};

SampleFamily sample_graph(bool ego_is_heir = false,
                          bool nephew_is_heir = false) {
  SampleFamily f;
  f.persons = sample_persons(ego_is_heir, nephew_is_heir);
  f.unions = sample_unions();
  f.graph = PersonGraph::build(f.persons, f.unions);
  return f;
}

std::string key_of(const PersonGraph& g, const std::string& ego,
                   const std::string& alter) {
  const Relationship rel = find_relationship(g, ego, alter);
  REQUIRE(rel.found);
  return relationship_key(rel, *g.find(ego));
}

MourningGrade mourning_of(const PersonGraph& g, const std::string& ego,
                          const std::string& alter) {
  const auto r = compute_mourning_degree(g, MourningQuery{ego, alter, "ming_qing"});
  REQUIRE(r);
  return r->grade;
}

std::string appellation_of(const PersonGraph& g, const std::string& ego,
                           const std::string& alter) {
  const auto r = compute_appellation(g, AppellationQuery{ego, alter});
  REQUIRE(r);
  return r->standard;
}

int gap_of(const PersonGraph& g, const std::string& ego,
           const std::string& alter) {
  const Relationship rel = find_relationship(g, ego, alter);
  REQUIRE(rel.found);
  return rel.generation_gap();
}

}  // namespace

TEST_CASE("世系图：世代计算（自始祖起算）", "[lineage][generation]") {
  const auto fx = sample_graph();
  const PersonGraph& g = fx.graph;
  const auto gens = compute_generations(g);

  REQUIRE(gens.at("g4") == 1);   // 始祖
  REQUIRE(gens.at("g3") == 2);
  REQUIRE(gens.at("g2") == 3);
  REQUIRE(gens.at("f") == 4);
  REQUIRE(gens.at("ego") == 5);
  REQUIRE(gens.at("tb") == 5);    // 堂兄同代
  REQUIRE(gens.at("tbf") == 4);   // 堂伯（父辈）
  REQUIRE(gens.at("ns") == 6);    // 侄子（子辈）
  REQUIRE(gens.at("gb") == 3);
  REQUIRE(gens.at("zbg") == 2);
  REQUIRE(gens.at("zc") == 4);

  REQUIRE(generation_of(g, "ego").value() == 5);
  REQUIRE(generation_no_gap(g, "ego", "f").value() == -1);
  REQUIRE(generation_no_gap(g, "ego", "ns").value() == 1);
  REQUIRE(generation_no_gap(g, "ego", "tb").value() == 0);

  // 子女排序：行几 → 出生日期（be 1870 → ego 1875 → zy 1880）
  const auto kids = g.children_of("f");
  REQUIRE(kids.size() == 3);
  REQUIRE(kids.at(0)->id == "be");
  REQUIRE(kids.at(1)->id == "ego");
  REQUIRE(kids.at(2)->id == "zy");

  // 不在图中
  const auto missing = generation_of(g, "nobody");
  REQUIRE_FALSE(missing);
  REQUIRE(missing.error().code() == ErrorCode::NotFound);
}

TEST_CASE("辈分差：由关系路径推出，姻亲不塌", "[lineage][generation]") {
  const auto fx = sample_graph();
  const PersonGraph& g = fx.graph;

  // 血亲：上行负、下行正、同辈 0
  REQUIRE(gap_of(g, "ego", "f") == -1);
  REQUIRE(gap_of(g, "ego", "g4") == -4);
  REQUIRE(gap_of(g, "ego", "ns") == 1);
  REQUIRE(gap_of(g, "ego", "be") == 0);

  // 姻亲：配偶步不升降，故夫妻同辈、姻亲随配偶计辈。
  // 这是回归点——曾误用「世代序号差」当辈分差，嫁入者因父母未录
  // 而塌到始祖辈，算出「母亲 -4 辈、丈夫 +3 辈」这类错值。
  REQUIRE(gap_of(g, "ego", "w") == 0);    // 妻
  REQUIRE(gap_of(g, "w", "ego") == 0);    // 夫
  REQUIRE(gap_of(g, "ego", "wf") == -1);  // 岳父
  REQUIRE(gap_of(g, "w", "f") == -1);     // 妻 → 公公
  REQUIRE(gap_of(g, "ego", "bew") == 0);  // 嫂（兄之妻）
  REQUIRE(gap_of(g, "ego", "zyh") == 0);  // 妹夫
  REQUIRE(gap_of(g, "ego", "gzh") == -1); // 姑父

  // 世代序号差与辈分差是两个口径：妻之父母未上溯到本支始祖，
  // 序号必塌陷，故只保证可算，不以其数值为辈分依据。
  REQUIRE(generation_no_gap(g, "ego", "w").has_value());
}

TEST_CASE("关系路径与规则键（九族）", "[lineage][relationship]") {
  const auto fx = sample_graph();
  const PersonGraph& g = fx.graph;

  REQUIRE(key_of(g, "ego", "f") == "F");
  REQUIRE(key_of(g, "ego", "m") == "M");
  REQUIRE(key_of(g, "ego", "b1") == "F.B+");
  REQUIRE(key_of(g, "ego", "b2") == "F.B-");
  REQUIRE(key_of(g, "ego", "gz") == "F.Z+");
  REQUIRE(key_of(g, "ego", "b1w") == "F.B+.W");
  REQUIRE(key_of(g, "ego", "b2w") == "F.B-.W");
  REQUIRE(key_of(g, "ego", "gzh") == "F.Z+.H");
  REQUIRE(key_of(g, "ego", "tb") == "F.B+.S+");
  REQUIRE(key_of(g, "ego", "td") == "F.B+.D-");
  REQUIRE(key_of(g, "ego", "tby") == "F.B-.S-");
  REQUIRE(key_of(g, "ego", "tbm") == "F.Z+.S+");
  REQUIRE(key_of(g, "ego", "tbmm") == "M.B+.D-");
  REQUIRE(key_of(g, "ego", "mj") == "M.B+");
  REQUIRE(key_of(g, "ego", "mf") == "M.F");
  REQUIRE(key_of(g, "ego", "be") == "B+");
  REQUIRE(key_of(g, "ego", "zy") == "Z-");
  REQUIRE(key_of(g, "ego", "bew") == "B+.W");
  REQUIRE(key_of(g, "ego", "zyh") == "Z-.H");
  REQUIRE(key_of(g, "ego", "ns") == "B+.S");
  REQUIRE(key_of(g, "ego", "nd") == "B+.D");
  REQUIRE(key_of(g, "ego", "ws") == "Z-.S");
  REQUIRE(key_of(g, "ego", "w") == "W");
  REQUIRE(key_of(g, "ego", "wf") == "W.F");
  REQUIRE(key_of(g, "w", "f") == "H.F");       // 公公（妻视角）
  REQUIRE(key_of(g, "w", "m") == "H.M");       // 婆婆
  REQUIRE(key_of(g, "ego", "wzh") == "W.Z+.H");  // 连襟
  REQUIRE(key_of(g, "ego", "gb") == "F.F.B+");
  REQUIRE(key_of(g, "ego", "tbf") == "F.F.B+.S+");
  REQUIRE(key_of(g, "ego", "g4") == "F.F.F.F");
  REQUIRE(key_of(g, "ego", "zc") == "F.F.F.B+.S.S-");

  // 语义解析：高祖路径上行 4 步
  const Relationship rel = find_relationship(g, "ego", "g4");
  REQUIRE(rel.found);
  REQUIRE(rel.up == 4);
  REQUIRE(rel.down == 0);
  REQUIRE_FALSE(rel.via_mother);
  REQUIRE_FALSE(rel.via_marriage);
  REQUIRE(rel.generation_gap() == -4);

  // 姻亲路径带婚姻边
  const Relationship affinal = find_relationship(g, "ego", "wf");
  REQUIRE(affinal.via_marriage);

  // 不可达：图中加入孤立节点
  auto persons = sample_persons();
  persons.push_back(mk("loner", Gender::Male, 1900, "", ""));
  const PersonGraph g2 = PersonGraph::build(persons, sample_unions());
  const auto none = find_relationship(g2, "ego", "loner");
  REQUIRE_FALSE(none.found);
}

TEST_CASE("五服计算（明清定型版快照）", "[lineage][mourning]") {
  const auto fx = sample_graph();
  const PersonGraph& g = fx.graph;

  REQUIRE(mourning_of(g, "ego", "f") == MourningGrade::ZhanCui);      // 斩衰
  REQUIRE(mourning_of(g, "ego", "m") == MourningGrade::ZhanCui);      // 明清子为母
  REQUIRE(mourning_of(g, "ego", "g2") == MourningGrade::QiCuiBuZhang);
  REQUIRE(mourning_of(g, "ego", "g3") == MourningGrade::QiCuiWuYue);
  REQUIRE(mourning_of(g, "ego", "g4") == MourningGrade::QiCuiSanYue);
  REQUIRE(mourning_of(g, "ego", "b1") == MourningGrade::QiCuiBuZhang);
  REQUIRE(mourning_of(g, "ego", "b2") == MourningGrade::QiCuiBuZhang);
  REQUIRE(mourning_of(g, "ego", "gz") == MourningGrade::DaGong);   // 已嫁姑
  REQUIRE(mourning_of(g, "ego", "mj") == MourningGrade::XiaoGong); // 舅
  REQUIRE(mourning_of(g, "ego", "mf") == MourningGrade::XiaoGong); // 外祖父
  REQUIRE(mourning_of(g, "ego", "be") == MourningGrade::QiCuiBuZhang);
  REQUIRE(mourning_of(g, "ego", "zy") == MourningGrade::DaGong);  // 已嫁妹
  REQUIRE(mourning_of(g, "ego", "tb") == MourningGrade::DaGong);   // 堂兄
  REQUIRE(mourning_of(g, "ego", "td") == MourningGrade::DaGong);   // 未嫁堂妹
  REQUIRE(mourning_of(g, "ego", "tbm") == MourningGrade::SiMa);    // 姑表兄
  REQUIRE(mourning_of(g, "ego", "tbmm") == MourningGrade::SiMa);   // 舅表妹
  REQUIRE(mourning_of(g, "ego", "ns") == MourningGrade::QiCuiBuZhang);  // 侄
  REQUIRE(mourning_of(g, "ego", "ws") == MourningGrade::XiaoGong);      // 外甥
  REQUIRE(mourning_of(g, "ego", "wf") == MourningGrade::SiMa);     // 岳父
  REQUIRE(mourning_of(g, "w", "f") == MourningGrade::ZhanCui);     // 妇为舅姑
  REQUIRE(mourning_of(g, "ego", "w") == MourningGrade::QiCuiZhang);  // 夫为妻
  REQUIRE(mourning_of(g, "zy", "f") == MourningGrade::QiCuiBuZhang);  // 已嫁女为父母
  REQUIRE(mourning_of(g, "ego", "gb") == MourningGrade::XiaoGong); // 伯祖父
  REQUIRE(mourning_of(g, "ego", "tbf") == MourningGrade::XiaoGong); // 堂伯

  // 承重孙（is_heir）为祖父：斩衰
  const auto heir_fx = sample_graph(/*ego_is_heir=*/true);
  const PersonGraph& heir_g = heir_fx.graph;
  REQUIRE(mourning_of(heir_g, "ego", "g2") == MourningGrade::ZhanCui);
  // 父为嫡长子：斩衰
  const auto heir_fx2 = sample_graph(false, /*nephew_is_heir=*/true);
  const PersonGraph& heir_g2 = heir_fx2.graph;
  REQUIRE(mourning_of(heir_g2, "be", "ns") == MourningGrade::ZhanCui);

  // 兜底：规则表未收录的族亲路径（父之父之父之兄之子之子）
  const auto fb = compute_mourning_degree(g, MourningQuery{"ego", "zc", "ming_qing"});
  REQUIRE(fb);
  REQUIRE(fb->grade == MourningGrade::SiMa);
  REQUIRE(fb->from_fallback);
  REQUIRE(fb->garment_name == "缌麻");

  // 错误路径
  const auto bad_ruleset =
      compute_mourning_degree(g, MourningQuery{"ego", "f", "song_dynasty"});
  REQUIRE_FALSE(bad_ruleset);
  REQUIRE(bad_ruleset.error().code() == ErrorCode::InvalidArgument);

  const auto self = compute_mourning_degree(g, MourningQuery{"ego", "ego", "ming_qing"});
  REQUIRE_FALSE(self);

  const auto missing =
      compute_mourning_degree(g, MourningQuery{"ego", "nobody", "ming_qing"});
  REQUIRE_FALSE(missing);
  REQUIRE(missing.error().code() == ErrorCode::NotFound);

  // 元数据
  const auto deg = compute_mourning_degree(g, MourningQuery{"ego", "f", "ming_qing"});
  REQUIRE(deg);
  REQUIRE(deg->garment_name == "斩衰");
  REQUIRE(deg->period == "三年");
  REQUIRE(deg->period_months == 27);
  REQUIRE(deg->ruleset == "ming_qing");
  REQUIRE_FALSE(deg->from_fallback);
  REQUIRE_FALSE(deg->basis.empty());

  // 缺省 ruleset（结构体默认成员初始化）= ming_qing
  const auto default_rs = compute_mourning_degree(g, MourningQuery{"ego", "f"});
  REQUIRE(default_rs);
  REQUIRE(default_rs->ruleset == "ming_qing");

  // 可用规则表
  const auto sets = available_rulesets();
  REQUIRE(sets.size() == 1);
  REQUIRE(sets.at(0) == "ming_qing");
}

TEST_CASE("称谓计算（九族快照）", "[lineage][appellation]") {
  const auto fx = sample_graph();
  const PersonGraph& g = fx.graph;

  const auto check = [&](const std::string& ego, const std::string& alter,
                         const std::string& standard,
                         const std::string& formal) {
    const auto r = compute_appellation(g, AppellationQuery{ego, alter});
    REQUIRE(r);
    REQUIRE(r->standard == standard);
    REQUIRE(r->formal == formal);
    REQUIRE_FALSE(r->from_fallback);
  };

  check("ego", "f", "父亲", "父");
  check("ego", "m", "母亲", "母");
  check("ego", "g2", "祖父", "祖父");
  check("ego", "b1", "伯父", "伯父");
  check("ego", "b2", "叔父", "叔父");
  check("ego", "gz", "姑母", "姑母");
  check("ego", "b1w", "伯母", "伯母");
  check("ego", "tb", "堂兄", "从父兄");
  check("ego", "tby", "堂弟", "从父弟");
  check("ego", "tbm", "表兄", "姑表兄");
  check("ego", "tbmm", "表妹", "舅表妹");
  check("ego", "mj", "舅父", "舅父");
  check("ego", "ns", "侄子", "侄");
  check("ego", "ws", "外甥", "外甥");
  check("ego", "wf", "岳父", "妻父");
  check("w", "f", "公公", "夫父");
  check("ego", "bew", "嫂子", "嫂");
  check("ego", "zyh", "妹夫", "妹夫");
  check("ego", "gb", "伯祖父", "伯祖父");
  check("ego", "tbf", "堂伯父", "从父伯");
  check("ego", "wzh", "连襟", "襟兄弟");
  check("ego", "be", "哥哥", "兄");
  check("ego", "zy", "妹妹", "妹");
  check("be", "ns", "儿子", "子");

  // 口语称谓抽查
  const auto tangge = compute_appellation(g, AppellationQuery{"ego", "tb"});
  REQUIRE(tangge);
  REQUIRE(tangge->colloquial == "堂哥");

  // 同一人
  const auto self = compute_appellation(g, AppellationQuery{"ego", "ego"});
  REQUIRE(self);
  REQUIRE(self->standard == "自己");

  // 兜底：族亲组合式描述
  const auto fb = compute_appellation(g, AppellationQuery{"ego", "zc"});
  REQUIRE(fb);
  REQUIRE(fb->standard == "父之父之父之兄之子之子");
  REQUIRE(fb->from_fallback);

  // 不可达
  const auto none = compute_appellation(g, AppellationQuery{"ego", "nobody"});
  REQUIRE_FALSE(none);
  REQUIRE(none.error().code() == ErrorCode::NotFound);
}

TEST_CASE("关系环不拖垮世代计算", "[lineage][cycle]") {
  // a 父 b、b 父 a：互环，世代无从推算，两节点均不入表
  std::vector<Person> v;
  v.push_back(mk("a", Gender::Male, 1850, "b", ""));
  v.push_back(mk("b", Gender::Male, 1820, "a", ""));
  v.push_back(mk("c", Gender::Male, 1880, "a", ""));
  const PersonGraph g = PersonGraph::build(v);
  const auto gens = compute_generations(g);
  REQUIRE(gens.find("a") == gens.end());
  REQUIRE(gens.find("b") == gens.end());
  REQUIRE(gens.find("c") == gens.end());  // 父母环上，亦不可达

  const auto r = generation_of(g, "a");
  REQUIRE_FALSE(r);
  REQUIRE(r.error().code() == ErrorCode::Internal);

  // 自环：父亲是自己
  std::vector<Person> v2;
  v2.push_back(mk("s", Gender::Male, 1850, "s", ""));
  const PersonGraph g2 = PersonGraph::build(v2);
  REQUIRE(compute_generations(g2).empty());
}

TEST_CASE("长幼信息缺失时的宽松匹配", "[lineage][seniority-unknown]") {
  // 兄弟二人均无出生日期与行次：键退化为 "B"，匹配展开 ± 变体
  std::vector<Person> v;
  Person elder;
  elder.id = "x";
  elder.gender = Gender::Male;
  Person younger;
  younger.id = "y";
  younger.gender = Gender::Male;
  v.push_back(elder);
  v.push_back(younger);
  const PersonGraph g = PersonGraph::build(v);
  // 无父母则不成兄弟；补共同父亲
  v.push_back(mk("p", Gender::Male, 1820, "", ""));
  v.at(0).father_id = "p";
  v.at(1).father_id = "p";
  const PersonGraph g2 = PersonGraph::build(v);
  const Relationship rel = find_relationship(g2, "x", "y");
  REQUIRE(rel.found);
  const std::string key = relationship_key(rel, *g2.find("x"));
  REQUIRE(key == "B");  // 长幼未知 → 无标记
  // 称谓仍可给出（展开 B+/B- 命中"哥哥/弟弟"之一）
  const auto app = compute_appellation(g2, AppellationQuery{"x", "y"});
  REQUIRE(app);
  REQUIRE_FALSE(app->from_fallback);
  const auto mour = compute_mourning_degree(g2, MourningQuery{"x", "y", "ming_qing"});
  REQUIRE(mour);
  REQUIRE(mour->grade == MourningGrade::QiCuiBuZhang);  // 为兄弟
}

TEST_CASE("妾步按角色判定，不靠性别猜（第五轮 P1-1）", "[lineage][relationship]") {
  std::vector<Person> persons;
  persons.push_back(mk("husband", Gender::Male, 1900, "", ""));
  persons.push_back(mk("wife", Gender::Female, 1902, "", ""));
  persons.push_back(mk("conc", Gender::Female, 1910, "", ""));
  std::vector<Union> unions;
  unions.push_back(couple("husband", MemberRole::Husband, "wife", MemberRole::Wife));
  // 妾室结合：妻位是 Wife、妾位是 Concubine
  unions.push_back(couple("husband", MemberRole::Husband, "conc", MemberRole::Concubine));
  const PersonGraph g = PersonGraph::build(persons, unions);

  // 夫看妾：路径是妾步（Q），规则表给"妾"，不再是"妻"的 W 键
  const auto rel = find_relationship(g, "husband", "conc", 4);
  REQUIRE(rel.found);
  const std::string key = relationship_key(rel, *g.find("husband"));
  CHECK(key.find('Q') != std::string::npos);
  CHECK(key.find('W') == std::string::npos);

  // 反向：妾看夫仍是 H（妾为夫，明清斩衰）
  const auto rev = find_relationship(g, "conc", "husband", 4);
  REQUIRE(rev.found);
  const std::string rkey = relationship_key(rev, *g.find("conc"));
  CHECK(rkey.find('H') != std::string::npos);
}
