#include <genealogy/io/synthetic.hpp>

#include <deque>

#include <genealogy/calendar/era.hpp>
#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

namespace genealogy::io {

namespace {

// 线性同余法：不追求统计品质，只追求「同种子同结果 + 够快」。
// 性能测试要的是可复现，不是密码学强度。
class Lcg {
 public:
  explicit Lcg(std::uint64_t seed) : s_(seed) {}
  std::uint32_t next() {
    s_ = s_ * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<std::uint32_t>(s_ >> 33);
  }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % n); }

 private:
  std::uint64_t s_;
};

const char* const kGivenPool[] = {
    "文远", "承嗣", "德昭", "世昌", "廷献", "国栋", "家修", "正清",
    "光裕", "明伦", "允成", "绍先", "维岳", "克勤", "敬之", "立本",
    "兆麟", "景行", "安邦", "定邦", "怀仁", "立志", "守经", "达权"};
constexpr std::size_t kGivenCount = sizeof(kGivenPool) / sizeof(kGivenPool[0]);

const char* const kCourtesyPool[] = {
    "子渊", "伯约", "叔夜", "季高", "元直", "公履", "彦士", "明远"};
constexpr std::size_t kCourtesyCount =
    sizeof(kCourtesyPool) / sizeof(kCourtesyPool[0]);

const char* const kPlaceNames[] = {"中湘", "颍川", "河东",  "吴兴",
                                   "江夏", "会稽", "陈留", "太原"};
constexpr std::size_t kPlaceCount = sizeof(kPlaceNames) / sizeof(kPlaceNames[0]);

}  // namespace

SyntheticData generate_synthetic(const SyntheticOptions& opt) {
  SyntheticData out;
  if (opt.lineage_id.empty()) return out;

  Lcg rng(opt.seed);
  const std::string now = utc_now_iso();

  for (std::size_t i = 0; i < kPlaceCount; ++i) {
    Place p;
    p.id = generate_ulid();
    p.name = kPlaceNames[i];
    p.type = PlaceType::Other;
    out.places.push_back(p);
  }
  const auto place_id = [&](int i) { return out.places[i].id; };

  // 世代内按「户」推进：每户一对夫妻 + 若干子女，子女再成家。
  // 用队列做广度优先，天然保证父母先于子女生成。
  struct PendingBirth {
    std::string father_id;
    std::string mother_id;
    int generation;
    int father_year;
    int mother_year;
  };
  std::deque<PendingBirth> queue;

  auto make_person = [&](Gender gender, int birth_year, int generation,
                         int birth_order, const std::string& father_id,
                         const std::string& mother_id) -> Person {
    Person p;
    p.id = generate_ulid();
    p.lineage_id = opt.lineage_id;
    p.family_name = opt.surname;
    p.given_name = kGivenPool[rng.below(static_cast<int>(kGivenCount))];
    if (rng.below(4) == 0) {
      p.courtesy_name = kCourtesyPool[rng.below(static_cast<int>(kCourtesyCount))];
    }
    p.gender = gender;
    p.generation_no = generation;
    p.birth_order = birth_order;
    p.birth_date = GenealogyDate::from_iso(std::to_string(birth_year)).value();
    p.birth_place_id = place_id(rng.below(static_cast<int>(kPlaceCount)));
    p.created_at = now;
    p.updated_at = now;
    p.rev = 1;
    if (!father_id.empty()) p.father_id = father_id;
    if (!mother_id.empty()) p.mother_id = mother_id;
    return p;
  };

  auto emit_facts = [&](const Person& p, int birth_year, int death_year) {
    // 出生/卒年/葬期三个事件；卒年之后若干年下葬
    Event birth;
    birth.id = generate_ulid();
    birth.subject_type = EventSubjectType::Person;
    birth.subject_id = p.id;
    birth.type = EventType::Birth;
    birth.date = p.birth_date;
    birth.place_id = p.birth_place_id;
    birth.created_at = now;
    out.events.push_back(birth);

    Event death;
    death.id = generate_ulid();
    death.subject_type = EventSubjectType::Person;
    death.subject_id = p.id;
    death.type = EventType::Death;
    death.date = GenealogyDate::from_iso(std::to_string(death_year)).value();
    death.created_at = now;
    out.events.push_back(death);

    Event burial;
    burial.id = generate_ulid();
    burial.subject_type = EventSubjectType::Person;
    burial.subject_id = p.id;
    burial.type = EventType::Burial;
    burial.date =
        GenealogyDate::from_iso(std::to_string(death_year + 1 + rng.below(3)))
            .value();
    burial.place_id = place_id(rng.below(static_cast<int>(kPlaceCount)));
    burial.created_at = now;
    out.events.push_back(burial);
  };

  // 始祖
  const int founder_death = opt.founder_year + 55 + rng.below(20);
  Person founder = make_person(Gender::Male, opt.founder_year, 1, 1, "", "");
  founder.death_date =
      GenealogyDate::from_iso(std::to_string(founder_death)).value();
  out.founder_id = founder.id;
  emit_facts(founder, opt.founder_year, founder_death);
  out.persons.push_back(founder);
  queue.push_back({founder.id, "", 2, opt.founder_year, 0});

  int safety = 0;
  const int max_iterations = static_cast<int>(opt.persons) * 4 + 1000;
  while (!queue.empty() && out.persons.size() < opt.persons &&
         safety++ < max_iterations) {
    const PendingBirth head = queue.front();
    queue.pop_front();
    if (head.generation > opt.generations) continue;

    // 户主本体（第一代已在库，这里补卒年并取出性别）
    Person* householder = nullptr;
    for (auto& p : out.persons) {
      if (p.id == head.father_id) householder = &p;
    }
    if (householder != nullptr && !householder->death_date) {
      const int fy = householder->birth_date.gregorian_year().value_or(
          opt.founder_year);
      const int d = fy + 55 + rng.below(20);
      householder->death_date =
          GenealogyDate::from_iso(std::to_string(d)).value();
    }
    // 户主性别决定配偶性别与结合角色：儿子娶妻、女儿招婿，都不产生
    // 同性结合。早先是「队首即丈夫、配偶一律女性」，女儿出队时就变成
    // 女性当丈夫——合成数据炸出一大片 R6，把真要人看的信号全淹了。
    const Gender head_gender =
        householder != nullptr ? householder->gender : Gender::Male;
    const Gender spouse_gender = head_gender == Gender::Male ? Gender::Female
                                                             : Gender::Male;

    // 成家：找一位年龄相仿的配偶
    const int spouse_year = head.father_year + rng.below(9) - 4;
    Person spouse = make_person(spouse_gender, spouse_year, head.generation, 1,
                                "", "");
    const int spouse_death = spouse_year + 60 + rng.below(15);
    spouse.death_date =
        GenealogyDate::from_iso(std::to_string(spouse_death)).value();
    emit_facts(spouse, spouse_year, spouse_death);
    out.persons.push_back(spouse);

    Union u;
    u.id = generate_ulid();
    u.type = UnionType::Marriage;
    u.status = UnionStatus::Active;
    u.start_date = GenealogyDate::from_iso(std::to_string(head.father_year + 20))
                       .value();
    u.members = head_gender == Gender::Male
                    ? std::vector<UnionMember>{
                          UnionMember{head.father_id, MemberRole::Husband},
                          UnionMember{spouse.id, MemberRole::Wife}}
                    : std::vector<UnionMember>{
                          UnionMember{spouse.id, MemberRole::Husband},
                          UnionMember{head.father_id, MemberRole::Wife}};
    out.unions.push_back(u);

    Event marriage;
    marriage.id = generate_ulid();
    marriage.subject_type = EventSubjectType::Union;
    marriage.subject_id = u.id;
    marriage.type = EventType::Marriage;
    marriage.date = u.start_date;
    marriage.created_at = now;
    out.events.push_back(marriage);

    // 子女：2..5 个，出生年在婚年后、母亲 50 岁前。
    // 儿女都立户，有效分叉≈3.5，撑 10 万人物 / 14 代有余。
    const int children = 2 + rng.below(4);
    const int first_child_year = head.father_year + 22;
    for (int c = 0; c < children; ++c) {
      if (out.persons.size() >= opt.persons) break;
      const int child_year =
          first_child_year + c * (2 + rng.below(3)) + rng.below(2);
      const Gender g = rng.below(2) == 0 ? Gender::Male : Gender::Female;
      // 父母按户主性别定：儿子娶妻则户主是父亲，女儿招婿则户主是母亲。
      // 早先一律写 father_id = 户主，女儿立户时就造出「女性当父亲」的
      // 数据——导出时 FAMC 挂靠不上（结合里的丈夫是配偶，不是户主），
      // 往返直接丢掉一半亲子链。
      const std::string child_father =
          head_gender == Gender::Male ? head.father_id : spouse.id;
      const std::string child_mother =
          head_gender == Gender::Male ? spouse.id : head.father_id;
      Person child = make_person(g, child_year, head.generation + 1, c + 1,
                                 child_father, child_mother);
      if (head.generation + 1 >= opt.generations) {
        // 末代不继续下钻，但仍要有卒年
        const int d = child_year + 50 + rng.below(25);
        child.death_date = GenealogyDate::from_iso(std::to_string(d)).value();
        emit_facts(child, child_year, d);
        out.persons.push_back(child);
        continue;
      }
      out.persons.push_back(child);
      // 子女都入队立户：儿子娶妻、女儿招婿，世系树因此覆盖全族而不
      // 只有男系。十代子孙树的节点数（8 千 vs 7 万）全系于此——
      // 只钻男系会让这项指标失去压力。
      queue.push_back({child.id, spouse.id, head.generation + 1, child_year,
                       spouse_year});
    }
  }
  return out;
}

}  // namespace genealogy::io
