// M1.5 GEDCOM 5.5.1 测试：语法层（字符集/续行/畸形）、映射层（人名/日期/
// 家族关系/损失报告）、写出层（255 字节切行）与端到端往返。

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/io/gedcom.hpp>

using namespace genealogy;
using namespace genealogy::io;

namespace {

// 一份最小但覆盖主要标签的谱：夫妻 + 两个子女 + 妾 + 多任 + 长备注
const char* kSample = R"GED(0 HEAD
1 SOUR TestSuite
2 VERS 1.0
1 CHAR UTF-8
1 GEDC
2 VERS 5.5.1
2 FORM LINEAGE-LINKED
0 @I1@ INDI
1 NAME 文远 /陈/
2 GIVN 文远
2 SURN 陈
1 SEX M
1 BIRT
2 DATE 9 MAY 1864
2 PLAC 湖南, 湘潭
1 DEAT
2 DATE ABT 1920
1 NOTE 首行
2 CONT 次行
2 CONC 接续
1 REFN A-001
1 FAMC @F1@
1 FAMS @F2@
1 CHAN
2 DATE 2020-01-02T03:04:05Z
0 @I2@ INDI
1 NAME 门查氏 /王/
1 SEX F
1 BIRT
2 DATE BET 1860 AND 1865
1 DEAT Y
1 FAMS @F2@
1 FAMS @F3@
0 @I3@ INDI
1 NAME 承嗣 /陈/
1 SEX M
1 FAMC @F2@
0 @I4@ INDI
1 NAME 婉娘 /陈/
1 SEX F
1 FAMC @F2@
0 @I5@ INDI
1 NAME 姨娘 /李/
1 SEX F
1 FAMS @F3@
0 @F2@ FAM
1 HUSB @I1@
1 WIFE @I2@
1 CHIL @I3@
1 CHIL @I4@
1 MARR
2 DATE 1883
2 PLAC 中湘
0 @F3@ FAM
1 HUSB @I1@
1 _CONC @I5@
1 CHIL @I3@
0 @F1@ FAM
1 HUSB @I9@
1 WIFE @I2@
0 TRLR
)GED";

const GedcomNode* find_rec(const GedcomDocument& doc,
                            const std::string& xref) {
  for (auto& r : doc.records) {
    if (r.xref == xref) return &r;
  }
  return nullptr;
}


// UTF-8 → UTF-16LE（字符集测试构造输入用）
std::string utf8_to_utf16le(const std::string& u8) {
  std::string out;
  std::size_t i = 0;
  while (i < u8.size()) {
    const unsigned char c = static_cast<unsigned char>(u8[i]);
    std::uint32_t cp = 0;
    int len = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1Fu;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0Fu;
      len = 3;
    } else {
      cp = c & 0x07u;
      len = 4;
    }
    for (int k = 1; k < len && i + static_cast<std::size_t>(k) < u8.size(); ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(u8[i + k]) & 0x3Fu);
    }
    i += static_cast<std::size_t>(len);
    if (cp >= 0x10000) {
      const std::uint32_t v = cp - 0x10000;
      const std::uint32_t hi = 0xD800 + (v >> 10);
      const std::uint32_t lo = 0xDC00 + (v & 0x3FF);
      out += static_cast<char>(hi & 0xFF);
      out += static_cast<char>((hi >> 8) & 0xFF);
      out += static_cast<char>(lo & 0xFF);
      out += static_cast<char>((lo >> 8) & 0xFF);
    } else {
      out += static_cast<char>(cp & 0xFF);
      out += static_cast<char>((cp >> 8) & 0xFF);
    }
  }
  return out;
}

}  // namespace

TEST_CASE("GEDCOM 语法：基本结构与标签层级") {
  const auto doc = parse_gedcom(kSample);
  REQUIRE(doc.has_value());
  CHECK(doc->encoding == "UTF-8");
  CHECK(doc->source_name == "TestSuite");
  CHECK(doc->source_version == "1.0");
  CHECK(doc->gedcom_version == "5.5.1");
  CHECK(doc->form == "LINEAGE-LINKED");

  // 记录数与顺序（TRLR 也是一条 level 0 记录）
  CHECK(doc->records.size() == 10);
  CHECK(doc->records.front().tag == "HEAD");
  CHECK(doc->records.back().tag == "TRLR");

  const GedcomNode* i1 = find_rec(*doc, "I1");
  REQUIRE(i1 != nullptr);
  CHECK(i1->tag == "INDI");
  CHECK(i1->level == 0);
  CHECK(i1->value.empty());

  // 子节点与孙节点
  const GedcomNode* name = i1->child("NAME");
  REQUIRE(name != nullptr);
  CHECK(name->value == "文远 /陈/");
  CHECK(name->level == 1);
  CHECK(name->child_value("GIVN") == "文远");
  CHECK(name->child_value("SURN") == "陈");

  const GedcomNode* birt = i1->child("BIRT");
  REQUIRE(birt != nullptr);
  CHECK(birt->child_value("DATE") == "9 MAY 1864");
  CHECK(birt->child_value("PLAC") == "湖南, 湘潭");
  CHECK(birt->child("DATE")->level == 2);

  // children_all
  CHECK(i1->children_all("NAME").size() == 1);
  CHECK(find_rec(*doc, "I1")->children_all("NOTE").size() == 1);

  // 不存在的标签
  CHECK(i1->child("NOPE") == nullptr);
  CHECK(i1->child_value("NOPE").empty());
}

TEST_CASE("GEDCOM 语法：CONT 换行与 CONC 接续") {
  const auto doc = parse_gedcom(kSample);
  REQUIRE(doc.has_value());
  const GedcomNode* i1 = find_rec(*doc, "I1");
  const GedcomNode* note = i1->child("NOTE");
  REQUIRE(note != nullptr);
  CHECK(note->value == "首行\n次行接续");
  // CONT/CONC 已合并，不应再作为子节点出现
  CHECK(note->child("CONT") == nullptr);
  CHECK(note->child("CONC") == nullptr);
}

TEST_CASE("GEDCOM 语法：字符集识别与拒绝") {
  // UTF-8 BOM
  {
    const std::string s = std::string("\xEF\xBB\xBF") + kSample;
    const auto doc = parse_gedcom(s);
    REQUIRE(doc.has_value());
    CHECK(doc->encoding == "UTF-8");
  }
  // CP1252：CHAR 声明 + 高位字节（£ = 0xA3）
  {
    std::string s = "0 HEAD\n1 CHAR CP1252\n0 @I1@ INDI\n1 NAME \xA3 /x/\n0 TRLR\n";
    const auto doc = parse_gedcom(s);
    REQUIRE(doc.has_value());
    CHECK(doc->encoding == "CP1252");
    const GedcomNode* i = find_rec(*doc, "I1");
    REQUIRE(i != nullptr);
    CHECK(i->child_value("NAME") == "\xC2\xA3 /x/");   // 0xA3 → U+00A3
  }
  // Latin-1
  {
    const std::string s = "0 HEAD\n1 CHAR LATIN1\n0 @I1@ INDI\n1 NAME \xE9 /x/\n0 TRLR\n";
    const auto doc = parse_gedcom(s);
    REQUIRE(doc.has_value());
    CHECK(doc->encoding == "LATIN1");
    CHECK(find_rec(*doc, "I1")->child_value("NAME") == "\xC3\xA9 /x/");
  }
  // UTF-16LE BOM（无 CHAR 声明）。UTF-16 按「字符」编码，
  // 把 UTF-8 的字节逐个加宽是错的——那样解出来是双倍乱码。
  {
    const std::string u8 = "0 HEAD\n0 @I1@ INDI\n1 NAME 文远 /陈/\n0 TRLR\n";
    const std::string s = std::string("\xFF\xFE") + utf8_to_utf16le(u8);
    const auto doc = parse_gedcom(s);
    REQUIRE(doc.has_value());
    CHECK(doc->encoding == "UTF-16LE");
    CHECK(find_rec(*doc, "I1")->child_value("NAME") == "文远 /陈/");
  }
  // ANSEL：明确拒绝，不猜字节
  {
    const std::string s = "0 HEAD\n1 CHAR ANSEL\n0 @I1@ INDI\n0 TRLR\n";
    const auto doc = parse_gedcom(s);
    REQUIRE(!doc.has_value());
    CHECK(doc.error().code() == ErrorCode::IoError);
    CHECK(doc.error().message().find("ANSEL") != std::string::npos);
  }
  // 未知字符集同样拒收
  {
    const std::string s = "0 HEAD\n1 CHAR MACINTOSH\n0 TRLR\n";
    const auto doc = parse_gedcom(s);
    REQUIRE(!doc.has_value());
    CHECK(doc.error().code() == ErrorCode::IoError);
  }
}

TEST_CASE("GEDCOM 语法：畸形输入一律报 6001") {
  // 没有 level 的行
  {
    const auto doc = parse_gedcom("这不是 GEDCOM\n");
    REQUIRE(!doc.has_value());
    CHECK(doc.error().code() == ErrorCode::IoError);
  }
  // 空内容
  {
    const auto doc = parse_gedcom("");
    REQUIRE(!doc.has_value());
    CHECK(doc.error().code() == ErrorCode::IoError);
  }
  // 只有空白行
  {
    const auto doc = parse_gedcom("\n\n\n");
    REQUIRE(!doc.has_value());
    CHECK(doc.error().code() == ErrorCode::IoError);
  }
}

TEST_CASE("GEDCOM 语法：level 跳级容忍，不报废整份文件") {
  const std::string s = "0 HEAD\n"
                        "0 @I1@ INDI\n"
                        "1 NAME 文远 /陈/\n"
                        "3 PLAC 越级地点\n"      // 从 1 跳到 3
                        "1 SEX M\n"
                        "0 TRLR\n";
  const auto doc = parse_gedcom(s);
  REQUIRE(doc.has_value());
  const GedcomNode* i1 = find_rec(*doc, "I1");
  REQUIRE(i1 != nullptr);
  // 跳级节点按「上一级子节点」挂进去，不丢
  const GedcomNode* name = i1->child("NAME");
  REQUIRE(name != nullptr);
  CHECK(name->child_value("PLAC") == "越级地点");
  CHECK(i1->child_value("SEX") == "M");
}

TEST_CASE("GEDCOM 映射：人名三种形态") {
  const auto doc = parse_gedcom(kSample);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());
  CHECK(r->persons.size() == 5);

  // 标准 /姓/ 结构
  const Person* wen = nullptr;
  const Person* wang = nullptr;
  const Person* cheng = nullptr;
  for (const auto& p : r->persons) {
    if (p.given_name == "文远") wen = &p;
    if (p.given_name == "门查氏") wang = &p;
    if (p.given_name == "承嗣") cheng = &p;
  }
  REQUIRE(wen != nullptr);
  CHECK(wen->family_name == "陈");
  CHECK(wen->given_name == "文远");
  REQUIRE(wang != nullptr);
  CHECK(wang->family_name == "王");
  CHECK(wang->given_name == "门查氏");

  // REFN 并入备注
  REQUIRE(wen->note.has_value());
  CHECK(wen->note->find("A-001") != std::string::npos);

  // CHAN.DATE → updated_at
  CHECK(wen->updated_at == "2020-01-02T03:04:05Z");
}

TEST_CASE("GEDCOM 映射：无 /姓/ 结构时按默认姓拆分") {
  const std::string s = "0 HEAD\n1 CHAR UTF-8\n"
                        "0 @I1@ INDI\n1 NAME 陈文远\n1 SEX M\n"
                        "0 @I2@ INDI\n1 NAME 王门查氏\n1 SEX F\n"
                        "0 TRLR\n";
  const auto doc = parse_gedcom(s);
  REQUIRE(doc.has_value());
  // 给了默认姓：前缀命中则拆开
  auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());
  const Person* p1 = nullptr;
  for (const auto& p : r->persons) {
    if (p.given_name == "文远") p1 = &p;
  }
  REQUIRE(p1 != nullptr);
  CHECK(p1->family_name == "陈");
  CHECK(p1->given_name == "文远");

  // 不给默认姓：整串记名 + 记 Info 损失
  auto r2 = import_gedcom(*doc, ImportOptions{"", true});
  REQUIRE(r2.has_value());
  bool found_loss = false;
  for (const auto& l : r2->losses) {
    if (l.tag == "NAME" && l.reason.find("整串记入") != std::string::npos) {
      found_loss = true;
      CHECK(l.severity == LossSeverity::Info);
    }
  }
  CHECK(found_loss);
  const Person* p2 = nullptr;
  for (const auto& p : r2->persons) {
    if (p.given_name == "陈文远") p2 = &p;
  }
  REQUIRE(p2 != nullptr);
  CHECK(p2->family_name.empty());
}

TEST_CASE("GEDCOM 映射：日期的各种形态") {
  const std::string s = "0 HEAD\n1 CHAR UTF-8\n"
                        "0 @I1@ INDI\n1 NAME 甲 /一/\n"
                        "1 BIRT\n2 DATE 9 MAY 1864\n"
                        "1 DEAT\n2 DATE 1920\n"
                        "0 @I2@ INDI\n1 NAME 乙 /二/\n"
                        "1 BIRT\n2 DATE ABT 1850\n"
                        "1 DEAT\n2 DATE BEF 1930\n"
                        "0 @I3@ INDI\n1 NAME 丙 /三/\n"
                        "1 BIRT\n2 DATE JUN 1870\n"
                        "0 @I4@ INDI\n1 NAME 丁 /四/\n"
                        "1 BIRT\n2 DATE 30 FEB 1900\n"
                        "0 @I5@ INDI\n1 NAME 戊 /五/\n"
                        "1 BIRT\n2 DATE INT 1900 (say)\n"
                        "0 @I6@ INDI\n1 NAME 己 /六/\n"
                        "1 BIRT\n2 DATE @#DJULIAN@ 5 JAN 1800\n"
                        "0 TRLR\n";
  const auto doc = parse_gedcom(s);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());

  auto by_given = [&](const std::string& g) -> const Person* {
    for (const auto& p : r->persons) {
      if (p.given_name == g) return &p;
    }
    return nullptr;
  };

  // 完整日：精度 Day，公历 yyyy-mm-dd
  const Person* p1 = by_given("甲");
  REQUIRE(p1 != nullptr);
  CHECK(p1->birth_date.precision == DatePrecision::Day);
  CHECK(p1->birth_date.gregorian.value() == "1864-05-09");
  CHECK(p1->birth_date.lunar.value() == "9 MAY 1864");   // 原文照录
  CHECK(p1->birth_date.ganzhi.value() == "甲子");        // 1864 = 甲子
  // 仅年份：精度 Year
  CHECK(p1->death_date->precision == DatePrecision::Year);
  CHECK(p1->death_date->gregorian.value() == "1920");

  // ABT → 约
  const Person* p2 = by_given("乙");
  REQUIRE(p2 != nullptr);
  CHECK(p2->birth_date.qualifier.value() == "约");
  CHECK(p2->birth_date.precision == DatePrecision::Year);
  // BEF → 以前
  CHECK(p2->death_date->qualifier.value() == "以前");

  // 月 + 年：精度 Month
  const Person* p3 = by_given("丙");
  REQUIRE(p3 != nullptr);
  CHECK(p3->birth_date.precision == DatePrecision::Month);
  CHECK(p3->birth_date.gregorian.value() == "1870-06");

  // 非法日（2 月 30 日）：降级到月 + 记 Warning
  const Person* p4 = by_given("丁");
  REQUIRE(p4 != nullptr);
  CHECK(p4->birth_date.precision == DatePrecision::Month);
  CHECK(p4->birth_date.gregorian.value() == "1900-02");
  bool bad_day = false;
  for (const auto& l : r->losses) {
    if (l.tag == "BIRT.DATE" && l.severity == LossSeverity::Warning) bad_day = true;
  }
  CHECK(bad_day);

  // INT 短语：只留原文
  const Person* p5 = by_given("戊");
  REQUIRE(p5 != nullptr);
  CHECK(p5->birth_date.precision == DatePrecision::Fuzzy);
  CHECK(p5->birth_date.gregorian.has_value() == false);
  CHECK(p5->birth_date.lunar.value() == "INT 1900 (say)");

  // 非格里历：原文标注 + Warning
  const Person* p6 = by_given("己");
  REQUIRE(p6 != nullptr);
  CHECK(p6->birth_date.precision == DatePrecision::Day);
  CHECK(p6->birth_date.gregorian.value() == "1800-01-05");
  bool julian = false;
  for (const auto& l : r->losses) {
    if (l.tag == "BIRT.DATE" && l.reason.find("格里历") != std::string::npos) {
      julian = true;
    }
  }
  CHECK(julian);
}

TEST_CASE("GEDCOM 映射：地点去重与经纬度") {
  const std::string s = "0 HEAD\n1 CHAR UTF-8\n"
                        "0 @I1@ INDI\n1 NAME 甲 /一/\n"
                        "1 BIRT\n2 PLAC 中湘\n3 MAP\n"
                        "4 LATI N27.83\n4 LONG E112.94\n"
                        "1 DEAT\n2 PLAC 中湘\n"
                        "0 TRLR\n";
  const auto doc = parse_gedcom(s);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());
  CHECK(r->places.size() == 1);   // 同名去重
  CHECK(r->places.front().name == "中湘");
  // 经纬度是浮点，用容差比（不引 Catch2 的 Approx，省一个头依赖）
  const double lat = r->places.front().lat.value();
  const double lng = r->places.front().lng.value();
  CHECK(lat > 27.829);
  CHECK(lat < 27.831);
  CHECK(lng > 112.939);
  CHECK(lng < 112.941);
  const Person* p = nullptr;
  for (const auto& x : r->persons) {
    if (x.given_name == "甲") p = &x;
  }
  REQUIRE(p != nullptr);
  REQUIRE(p->birth_place_id.has_value());
  CHECK(*p->birth_place_id == r->places.front().id);
  CHECK(*p->death_place_id == *p->birth_place_id);
}

TEST_CASE("GEDCOM 映射：家族关系、多任与妾") {
  const auto doc = parse_gedcom(kSample);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());

  // F2：夫妻 + 两个子女
  const Union* f2 = nullptr;
  const Union* f3 = nullptr;
  for (const auto& u : r->unions) {
    if (u.members.size() == 2 && u.members[0].role == MemberRole::Husband &&
        u.members[1].role == MemberRole::Wife) {
      f2 = &u;
    }
    if (u.members.size() == 2 && u.members[0].role == MemberRole::Husband &&
        u.members[1].role == MemberRole::Concubine) {
      f3 = &u;
    }
  }
  REQUIRE(f2 != nullptr);
  CHECK(f2->members.size() == 2);
  REQUIRE(f3 != nullptr);
  CHECK(f3->members[1].role == MemberRole::Concubine);

  // 子女反推父母
  const Person* cheng = nullptr;
  for (const auto& p : r->persons) {
    if (p.given_name == "承嗣") cheng = &p;
  }
  REQUIRE(cheng != nullptr);
  REQUIRE(cheng->father_id.has_value());
  REQUIRE(cheng->mother_id.has_value());
  // 父亲即 F2 的 HUSB
  CHECK(*cheng->father_id == f2->members[0].person_id);
  CHECK(*cheng->mother_id == f2->members[1].person_id);

  // 多任：I1 出现在 F2/F3 → 两任都编号
  CHECK(f2->sequence.has_value());
  CHECK(f3->sequence.has_value());
  CHECK(*f2->sequence == 1);
  CHECK(*f3->sequence == 2);

  // MARR 日期 → 结合开始日期 + 事件
  REQUIRE(f2->start_date.has_value());
  CHECK(f2->start_date->precision == DatePrecision::Year);
  CHECK(f2->start_date->gregorian.value() == "1883");
  int marr_events = 0;
  for (const auto& e : r->events) {
    if (e.type == EventType::Marriage && e.subject_type == EventSubjectType::Union) {
      ++marr_events;
    }
  }
  CHECK(marr_events == 1);

  // F1 的 HUSB 引用了不存在的 @I9@ → 结合降为单成员导入（保住母子链）+ Warning
  int wife_only = 0;
  for (const auto& u : r->unions) {
    if (u.members.size() == 1 && u.members[0].role == MemberRole::Wife) ++wife_only;
  }
  CHECK(wife_only == 1);
  bool missing_husb = false;
  for (const auto& l : r->losses) {
    if (l.tag == "HUSB" && l.severity == LossSeverity::Warning) missing_husb = true;
  }
  CHECK(missing_husb);
}

TEST_CASE("GEDCOM 映射：损失显式化，绝不静默丢弃") {
  const std::string s = "0 HEAD\n1 CHAR UTF-8\n"
                        "0 @I1@ INDI\n1 NAME 甲 /一/\n"
                        "1 OBJE @M1@\n"
                        "1 SOUR @S1@\n"
                        "1 _UID 12345\n"
                        "1 ASSO @I2@\n"
                        "1 ALIA 曾用名\n"
                        "0 @S1@ SOUR\n1 TITLE 族谱抄本\n"
                        "0 TRLR\n";
  const auto doc = parse_gedcom(s);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());

  auto has = [&](const std::string& tag, LossSeverity sev) {
    for (const auto& l : r->losses) {
      if (l.tag == tag && l.severity == sev) return true;
    }
    return false;
  };
  CHECK(has("OBJE", LossSeverity::Warning));      // 附件未建模
  // M7.5：SOUR 已建模（0 SOUR 记录 + INDI 1 SOUR → 出处链），不再进损失
  for (const auto& l : r->losses) {
    CHECK(l.tag != "SOUR");
  CHECK(has("_UID", LossSeverity::Info));
  CHECK(has("ASSO", LossSeverity::Info));
  }
  // 出处链：0 @S1@ SOUR 的 TITLE → 来源；INDI 1 SOUR → Person 级引用
  REQUIRE(r->sources.size() == 1);
  CHECK(r->sources[0].title == "族谱抄本");
  REQUIRE(r->citations.size() == 1);
  CHECK(r->citations[0].subject_type == CitationSubjectType::Person);
  CHECK(r->citations[0].source_id == r->sources[0].id);
  CHECK(r->citations[0].confidence == Confidence::Medium);  // 无 QUAY 缺省
  CHECK_FALSE(r->citations[0].locator.has_value());

  // ALIA 有字段，不算损失
  const Person* p = nullptr;
  for (const auto& x : r->persons) {
    if (x.given_name == "甲") p = &x;
  }
  REQUIRE(p != nullptr);
  REQUIRE(p->alias.has_value());
  CHECK(p->alias->find("曾用名") != std::string::npos);
}

TEST_CASE("GEDCOM 映射：死亡标记与性别") {
  const auto doc = parse_gedcom(kSample);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());
  for (const auto& p : r->persons) {
    if (p.given_name == "门查氏") {
      // GEDCOM 5.5.1：Y = 死亡事件已发生（日期未知）→ 已故，不是在世
      CHECK_FALSE(p.is_living);   // "1 DEAT Y"
      CHECK(p.gender == Gender::Female);
    }
    if (p.given_name == "文远") {
      // "1 DEAT" + DATE：有死亡事实，同样已故
      CHECK_FALSE(p.is_living);
      CHECK(p.gender == Gender::Male);
    }
    if (p.given_name == "承嗣") {
      // 无任何 DEAT 行：在世状态未知，模型默认不启用在世保护
      CHECK_FALSE(p.is_living);
    }
  }
}

TEST_CASE("GEDCOM 映射：DEAT N 显式在世") {
  const char* doc_text = R"GED(0 HEAD
1 CHAR UTF-8
1 GEDC
2 VERS 5.5.1
0 @I1@ INDI
1 NAME 现存 /陈/
1 SEX M
1 DEAT N
0 @I2@ INDI
1 NAME 未知 /陈/
1 SEX F
0 TRLR)GED";
  const auto doc = parse_gedcom(doc_text);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());
  for (const auto& p : r->persons) {
    if (p.given_name == "现存") {
      CHECK(p.is_living);   // DEAT N = 未死亡，显式在世保护
    }
    if (p.given_name == "未知") {
      CHECK_FALSE(p.is_living);   // 无 DEAT：未知
    }
  }
}

TEST_CASE("GEDCOM 写出：在世写 DEAT N，未知不写") {
  Person living;
  living.id = "01J00000000000000000000LA";
  living.family_name = "陈";
  living.given_name = "在世";
  living.gender = Gender::Male;
  living.is_living = true;
  Person unknown;
  unknown.id = "01J00000000000000000000DB";
  unknown.family_name = "陈";
  unknown.given_name = "未知";
  unknown.gender = Gender::Male;   // is_living 默认 false，无 death_date

  std::vector<Person> only_living = {living};
  const std::string out_living = export_gedcom(
      ExportOptions{&only_living, {}, {}, {}, {}, {}, "谱师", "1.0"});
  // 在世：显式 DEAT N（标准语义「未死亡」）；绝不写 DEAT Y——
  // 那会让第三方软件把活人读成死人
  CHECK(out_living.find("1 DEAT N") != std::string::npos);
  CHECK(out_living.find("1 DEAT Y") == std::string::npos);
  // 往返：DEAT N 导入回在世保护
  const auto doc = parse_gedcom(out_living);
  REQUIRE(doc.has_value());
  const auto back = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(back.has_value());
  REQUIRE(back->persons.size() == 1);
  CHECK(back->persons[0].is_living);

  std::vector<Person> only_unknown = {unknown};
  const std::string out_unknown = export_gedcom(
      ExportOptions{&only_unknown, {}, {}, {}, {}, {}, "谱师", "1.0"});
  // 未知（未登记在世）：不写 DEAT，避免误标为已故
  CHECK(out_unknown.find("1 DEAT") == std::string::npos);
}

TEST_CASE("GEDCOM 写出：结构与 255 字节切行") {
  std::vector<Person> persons;
  Person p;
  p.id = "01J0000000000000000000001";
  p.family_name = "陈";
  p.given_name = "文远";
  p.gender = Gender::Male;
  p.birth_date = GenealogyDate::from_iso("1864-05-09").value();
  p.updated_at = "2020-01-02T03:04:05Z";
  // 超长备注：含换行 + 中文（UTF-8 3 字节/字）
  std::string long_note = "谱师按语：";
  while (long_note.size() < 900) long_note += "陈氏源出颍川，历三十余世。";
  long_note += "\n第二段也要很长：" + std::string(200, 'x');
  p.note = long_note;
  persons.push_back(p);

  const std::string out = export_gedcom(ExportOptions{&persons, nullptr, nullptr,
                                                      nullptr, {}, {}, "谱师", "1.0"});

  // 每行都不超过 255 字节
  int line_no = 0;
  std::size_t pos = 0;
  while (pos < out.size()) {
    std::size_t eol = out.find('\n', pos);
    if (eol == std::string_view::npos) eol = out.size();
    const std::string line = out.substr(pos, eol - pos);
    ++line_no;
    INFO("第 " << line_no << " 行长度 " << line.size());
    CHECK(line.size() <= 255);
    pos = eol + 1;
  }
  CHECK(line_no > 10);

  // 结构要件
  CHECK(out.find("0 HEAD") != std::string::npos);
  CHECK(out.find("2 VERS 5.5.1") != std::string::npos);
  CHECK(out.find("1 CHAR UTF-8") != std::string::npos);
  CHECK(out.find("0 @I1@ INDI") != std::string::npos);
  CHECK(out.find("1 NAME 文远 /陈/") != std::string::npos);
  CHECK(out.find("2 SURN 陈") != std::string::npos);
  CHECK(out.find("1 SEX M") != std::string::npos);
  CHECK(out.find("2 DATE 9 MAY 1864") != std::string::npos);
  CHECK(out.find("0 TRLR") != std::string::npos);

  // 切行必须发生在 UTF-8 字符边界上：重新解析不应出现替换字符
  const auto doc = parse_gedcom(out);
  REQUIRE(doc.has_value());
  const GedcomNode* i1 = find_rec(*doc, "I1");
  REQUIRE(i1 != nullptr);
  const std::string note = i1->child_value("NOTE");
  CHECK(note == long_note);   // 原文完整回来
  CHECK(note.find("\xEF\xBF\xBD") == std::string::npos);   // 无 U+FFFD
}

TEST_CASE("GEDCOM 往返：导出再导入，信息不丢") {
  // 造一批领域对象（含妾、事件、地点、离异）
  std::vector<Person> persons;
  std::vector<Union> unions;
  std::vector<Event> events;
  std::vector<Place> places;

  Place pl;
  pl.id = "P1";
  places.push_back(pl);

  Person father;
  father.id = "I-FATHER";
  father.family_name = "陈";
  father.given_name = "文远";
  father.gender = Gender::Male;
  father.birth_date = GenealogyDate::from_iso("1864-05-09").value();
  father.death_date = GenealogyDate::from_iso("1920-01-01").value();
  father.updated_at = "2020-01-02T03:04:05Z";

  Person mother;
  mother.id = "I-MOTHER";
  mother.family_name = "王";
  mother.given_name = "门查氏";
  mother.gender = Gender::Female;
  mother.birth_date = GenealogyDate::from_iso("1866-03-03").value();

  Person child;
  child.id = "I-CHILD";
  child.family_name = "陈";
  child.given_name = "承嗣";
  child.gender = Gender::Male;
  child.father_id = father.id;
  child.mother_id = mother.id;
  child.birth_date = GenealogyDate::from_iso("1888-08-08").value();

  Person concubine;
  concubine.id = "I-CONC";
  concubine.family_name = "李";
  concubine.given_name = "姨娘";
  concubine.gender = Gender::Female;

  persons = {father, mother, child, concubine};

  Union main;
  main.id = "F-MAIN";
  main.members = {UnionMember{father.id, MemberRole::Husband},
                  UnionMember{mother.id, MemberRole::Wife}};
  main.start_date = GenealogyDate::from_iso("1883-01-01").value();
  main.place_id = pl.id;

  Union second;
  second.id = "F-SECOND";
  second.members = {UnionMember{father.id, MemberRole::Husband},
                    UnionMember{concubine.id, MemberRole::Concubine}};
  second.status = UnionStatus::Dissolved;
  second.end_date = GenealogyDate::from_iso("1905-01-01").value();

  unions = {main, second};

  Event burial;
  burial.id = "E1";
  burial.subject_type = EventSubjectType::Person;
  burial.subject_id = father.id;
  burial.type = EventType::Burial;
  burial.place_id = pl.id;
  events.push_back(burial);

  const std::string out = export_gedcom(
      ExportOptions{&persons, &unions, &events, &places, {}, {}, "谱师", "1.0"});

  // 导出文本应能自洽：妾用扩展标签，离异有 DIV
  CHECK(out.find("1 _CONC @I4@") != std::string::npos);
  CHECK(out.find("1 DIV") != std::string::npos);
  CHECK(out.find("2 PLAC 中湘") != std::string::npos);

  // 反向导入
  const auto doc = parse_gedcom(out);
  REQUIRE(doc.has_value());
  const auto back = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(back.has_value());

  CHECK(back->persons.size() == persons.size());
  CHECK(back->unions.size() == unions.size());
  CHECK(back->places.size() == 1);
  // 自家格式往返：不允许任何 Warning（那意味着真有信息丢失）；
  // 唯一允许的 Info 是 DIV——GEDCOM 的「离异事件」没有专属事件类型，
  // 只能落成结合状态 + 结束日期，这一点本来就该显式告知谱师。
  for (const auto& l : back->losses) {
    INFO("loss: " << l.xref << " " << l.tag << " " << l.reason);
    CHECK(l.severity == LossSeverity::Info);
    CHECK(l.tag == "DIV");
  }

  auto find_by_given = [](const std::vector<Person>& v,
                          const std::string& g) -> const Person* {
    for (const auto& p : v) {
      if (p.given_name == g) return &p;
    }
    return nullptr;
  };

  const Person* bf = find_by_given(back->persons, "文远");
  const Person* bm = find_by_given(back->persons, "门查氏");
  const Person* bc = find_by_given(back->persons, "承嗣");
  const Person* bl = find_by_given(back->persons, "姨娘");
  REQUIRE(bf != nullptr);
  REQUIRE(bm != nullptr);
  REQUIRE(bc != nullptr);
  REQUIRE(bl != nullptr);

  CHECK(bf->family_name == "陈");
  CHECK(bf->gender == Gender::Male);
  CHECK(bf->birth_date.gregorian.value() == "1864-05-09");
  CHECK(bf->death_date->gregorian.value() == "1920-01-01");
  CHECK(bf->updated_at == "2020-01-02T03:04:05Z");
  CHECK(bm->birth_date.gregorian.value() == "1866-03-03");

  // 父母链
  REQUIRE(bc->father_id.has_value());
  REQUIRE(bc->mother_id.has_value());
  CHECK(*bc->father_id == bf->id);
  CHECK(*bc->mother_id == bm->id);

  // 两任结合 + 妾角色 + 离异
  int main_found = 0, second_found = 0;
  for (const auto& u : back->unions) {
    bool has_father = false, has_mother = false, has_conc = false;
    for (const auto& m : u.members) {
      if (m.person_id == bf->id) has_father = true;
      if (m.person_id == bm->id) has_mother = true;
      if (m.person_id == bl->id) has_conc = true;
    }
    if (has_father && has_mother) {
      ++main_found;
      CHECK(u.start_date.has_value());
      CHECK(u.start_date->gregorian.value() == "1883-01-01");
      CHECK(u.sequence.value() == 1);
      CHECK(u.status == UnionStatus::Active);
    }
    if (has_father && has_conc) {
      ++second_found;
      CHECK(u.status == UnionStatus::Dissolved);
      CHECK(u.end_date->gregorian.value() == "1905-01-01");
      CHECK(u.sequence.value() == 2);
    }
  }
  CHECK(main_found == 1);
  CHECK(second_found == 1);

  // 事件也回来了（墓葬不在 person 字段里，应作为 BURI 事件导出并导入）
  int burial_back = 0;
  for (const auto& e : back->events) {
    if (e.type == EventType::Burial && e.subject_id == bf->id) ++burial_back;
  }
  CHECK(burial_back == 1);
}

TEST_CASE("GEDCOM 导出：部分导出的损失台账") {
  // 三人两结合：B 缺集时，涉及 B 的成员/亲子/事件都必须记账
  std::vector<Person> persons;
  Person a;
  a.id = "A1";
  a.family_name = "陈";
  a.given_name = "甲";
  a.gender = Gender::Male;
  Person b;
  b.id = "B2";
  b.family_name = "李";
  b.given_name = "乙";
  b.gender = Gender::Female;
  b.is_living = true;
  Person c;
  c.id = "C3";
  c.family_name = "陈";
  c.given_name = "丙";
  c.gender = Gender::Male;
  c.father_id = "A1";
  c.mother_id = "B2";
  persons = {a, c};   // 故意只导出 A 和 C，B 缺集

  Union fam;
  fam.id = "F1";
  fam.members = {UnionMember{"A1", MemberRole::Husband},
                 UnionMember{"B2", MemberRole::Wife}};
  Union fam2;
  fam2.id = "F2";
  fam2.members = {UnionMember{"B2", MemberRole::Husband}};
  std::vector<Union> unions = {fam, fam2};

  Event ev;
  ev.id = "E1";
  ev.subject_type = EventSubjectType::Union;
  ev.subject_id = "F2";
  ev.type = EventType::Marriage;
  std::vector<Event> events = {ev};

  std::vector<io::MappingLoss> losses;
  const std::string out = io::export_gedcom(
      io::ExportOptions{&persons, &unions, &events, nullptr, {}, {},
                        "谱师", "1.0", {}, false, &losses});

  // B 缺集 → WIFE 成员链缺失、F2 结合缺 HUSB、C 的母链无法表达
  int wife_loss = 0, husb_loss = 0, famc_loss = 0, event_loss = 0;
  for (const auto& l : losses) {
    if (l.tag == "WIFE") ++wife_loss;
    if (l.tag == "HUSB") ++husb_loss;
    if (l.tag == "FAMC") ++famc_loss;
    if (l.tag == "EVENT") ++event_loss;
  }
  CHECK(wife_loss >= 1);   // F1 的 WIFE（B 缺集）
  CHECK(husb_loss >= 1);   // F2 的 HUSB（B 缺集）
  CHECK(famc_loss >= 1);   // C 的母链（B 缺集）无法表达
  // E1 的主体是 F2，而 F2 在导出集内（只要有一名成员在集）——
  // 事件可挂靠，不触发 EVENT 损失；EVENT 损失只在主体整体缺集时出现
  CHECK(event_loss == 0);
  // 台账不许空：至少 3 条 Warning
  CHECK(losses.size() >= 3);
}

  const auto doc = parse_gedcom(kSample);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(!r.has_value());
  CHECK(r.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("GEDCOM 映射：世系环拒绝并点名，不报误导性的「父亲不存在」") {
  // 环数据落库后 order_by_dependency 排不出依赖顺序，曾让用例层报
  // 「父亲不存在」——对谱师是误导（病因是环，不是缺人）。映射层
  // 提前拦下、点名环上人物（2026-10-23 端到端测试暴露）。
  const std::string kCycle = R"GED(0 HEAD
1 CHAR UTF-8
0 @I1@ INDI
1 NAME 互环甲 /陈/
1 SEX M
0 @I2@ INDI
1 NAME 互环乙 /陈/
1 SEX M
0 @F1@ FAM
1 HUSB @I1@
1 CHIL @I2@
0 @F2@ FAM
1 HUSB @I2@
1 CHIL @I1@
0 TRLR
)GED";
  const auto doc = parse_gedcom(kCycle);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(!r.has_value());
  CHECK(r.error().code() == ErrorCode::InvalidArgument);
  const std::string msg = r.error().message();
  CHECK(msg.find("世系环") != std::string::npos);
  CHECK(msg.find("互环") != std::string::npos);

  // 自指同罪
  const std::string kSelf = R"GED(0 HEAD
1 CHAR UTF-8
0 @I9@ INDI
1 NAME 自指 /陈/
0 @F9@ FAM
1 HUSB @I9@
1 CHIL @I9@
0 TRLR
)GED";
  const auto doc2 = parse_gedcom(kSelf);
  REQUIRE(doc2.has_value());
  const auto r2 = import_gedcom(*doc2, ImportOptions{"陈", true});
  REQUIRE(!r2.has_value());
  CHECK(r2.error().message().find("世系环") != std::string::npos);

  // 无环的正常谱不受影响（回归守护：别把拦截做宽）
  const auto ok_doc = parse_gedcom(kSample);
  REQUIRE(ok_doc.has_value());
  const auto ok = import_gedcom(*ok_doc, ImportOptions{"陈", true});
  CHECK(ok.has_value());
}

TEST_CASE("GEDCOM 出处链：QUAY/PAGE 映射 + 往返零损失") {
  const std::string s =
      "0 HEAD\n1 CHAR UTF-8\n"
      "0 @S1@ SOUR\n1 TITL 中湘陈氏族谱\n1 AUTH 修谱局\n1 PUBL 上海图书馆\n"
      "0 @I1@ INDI\n1 NAME 甲 /一/\n"
      "1 BIRT\n2 DATE 1900 JAN 1\n2 SOUR @S1@\n3 PAGE 卷三 p136\n3 QUAY 3\n"
      "1 SOUR @S1@\n2 QUAY 1\n"
      "0 TRLR\n";
  const auto doc = parse_gedcom(s);
  REQUIRE(doc.has_value());
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());

  // 事实级 2 SOUR → 事件引用（QUAY 3→high、PAGE→locator）；
  // INDI 级 1 SOUR → 人物引用（QUAY 1→low）
  REQUIRE(r->sources.size() == 1);
  CHECK(r->sources[0].title == "中湘陈氏族谱");
  CHECK(r->sources[0].author.has_value());
  CHECK(*r->sources[0].author == "修谱局");
  CHECK(r->sources[0].repository.has_value());
  CHECK(*r->sources[0].repository == "上海图书馆");
  REQUIRE(r->citations.size() == 2);
  const Citation* ev_cite = nullptr;
  const Citation* per_cite = nullptr;
  for (const auto& c : r->citations) {
    if (c.subject_type == CitationSubjectType::Event) ev_cite = &c;
    if (c.subject_type == CitationSubjectType::Person) per_cite = &c;
  }
  REQUIRE(ev_cite != nullptr);
  CHECK(ev_cite->confidence == Confidence::High);
  REQUIRE(ev_cite->locator.has_value());
  CHECK(*ev_cite->locator == "卷三 p136");
  REQUIRE(r->events.size() == 1);  // BIRT 事件
  CHECK(ev_cite->subject_id == r->events[0].id);
  REQUIRE(per_cite != nullptr);
  CHECK(per_cite->confidence == Confidence::Low);
  CHECK_FALSE(per_cite->locator.has_value());
  CHECK(per_cite->source_id == r->sources[0].id);

  // 往返：导出（含出处链）→ 再导入 → 来源与引用逐字段稳定
  io::ExportOptions eo;
  eo.persons = &r->persons;
  eo.unions = &r->unions;
  eo.events = &r->events;
  eo.sources = &r->sources;
  eo.citations = &r->citations;
  const std::string out = export_gedcom(eo);
  CHECK(out.find("0 @S1@ SOUR") != std::string::npos);
  CHECK(out.find("1 SOUR @S1@") != std::string::npos);
  CHECK(out.find("2 SOUR @S1@") != std::string::npos);
  CHECK(out.find("3 QUAY 3") != std::string::npos);
  CHECK(out.find("3 PAGE 卷三 p136") != std::string::npos);

  const auto doc2 = parse_gedcom(out);
  REQUIRE(doc2.has_value());
  const auto r2 = import_gedcom(*doc2, ImportOptions{"陈", true});
  REQUIRE(r2.has_value());
  CHECK(r2->sources.size() == 1);
  CHECK(r2->sources[0].title == "中湘陈氏族谱");
  CHECK(r2->sources[0].author.has_value());
  CHECK(*r2->sources[0].author == "修谱局");
  CHECK(r2->sources[0].repository.has_value());
  CHECK(*r2->sources[0].repository == "上海图书馆");
  REQUIRE(r2->citations.size() == 2);
  const Citation* ev2 = nullptr;
  const Citation* per2 = nullptr;
  for (const auto& c : r2->citations) {
    if (c.subject_type == CitationSubjectType::Event) ev2 = &c;
    if (c.subject_type == CitationSubjectType::Person) per2 = &c;
  }
  REQUIRE(ev2 != nullptr);
  REQUIRE(per2 != nullptr);
  CHECK(ev2->confidence == Confidence::High);
  CHECK(ev2->locator.has_value());
  CHECK(*ev2->locator == "卷三 p136");
  CHECK(per2->confidence == Confidence::Low);
  CHECK_FALSE(per2->locator.has_value());
}


TEST_CASE("GEDCOM 7.0：写出 HEAD/SPEC + 无 CONC + 往返零损失") {
  // 构造一份含长备注（5.5.1 会 CONC 切行）的谱，导出 7.0
  std::vector<Person> persons;
  Person p;
  p.id = "P7";
  p.family_name = "陈";
  p.given_name = "七世";
  p.gender = Gender::Male;
  p.birth_date = GenealogyDate::from_iso("1900").value();
  std::string long_note;  // 400 个「长」= 1200 UTF-8 字节，5.5.1 下必 CONC 切行
  for (int i = 0; i < 100; ++i) long_note += "长长长长";
  p.note = long_note;
  persons.push_back(p);

  io::ExportOptions eo;
  eo.persons = &persons;
  eo.gedcom_version = "7.0";
  const std::string out = export_gedcom(eo);
  REQUIRE(!out.empty());

  // 7.0 头：VERS 7.0 + 必填 SPEC + 方言声明
  CHECK(out.find("2 VERS 7.0") != std::string::npos);
  CHECK((out.find("HEAD.SPEC") != std::string::npos ||
         out.find("SPEC https://gedcom.io") != std::string::npos));
  CHECK(out.find(" CONC ") == std::string::npos);  // 7.0 无 CONC
  CHECK(out.find("长长长长") != std::string::npos);  // 长备注完整在行内

  // 自家读取器读回：无损
  const auto doc = parse_gedcom(out);
  REQUIRE(doc.has_value());
  CHECK(doc->gedcom_version == "7.0");
  const auto r = import_gedcom(*doc, ImportOptions{"陈", true});
  REQUIRE(r.has_value());
  REQUIRE(r->persons.size() == 1);
  CHECK(r->persons[0].note.has_value());
  CHECK(r->persons[0].note->size() == 1200);  // 400 个「长」× 3 字节
  CHECK(r->persons[0].given_name == "七世");
}

TEST_CASE("GEDCOM：导出拒绝不支持的版本") {
  std::vector<Person> persons;
  persons.push_back([] {
    Person p;
    p.id = "P";
    p.family_name = "陈";
    p.given_name = "甲";
    p.gender = Gender::Male;
    return p;
  }());
  io::ExportOptions eo;
  eo.persons = &persons;
  eo.gedcom_version = "6.6";  // 不存在的版本
  CHECK(export_gedcom(eo).empty());
}