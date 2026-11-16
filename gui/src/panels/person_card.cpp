#include <genealogy/gui/panels/person_card.hpp>

#include <imgui.h>

#include <genealogy/gui/widgets.hpp>

#include <algorithm>

#include <genealogy/model/person.hpp>
#include <genealogy/repository/person_name_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/ulid.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

namespace {

// optional<string> ↔ InputText 缓冲的桥接。空串表示「未设置」——
// 与 core 的口径一致（service 层会把空串规范化为未设置）。
std::string opt_str(const std::optional<std::string>& v) {
  return v.value_or(std::string{});
}

std::optional<std::string> to_opt(const std::string& s) {
  if (s.empty()) return std::nullopt;
  return s;
}

int parse_int_or(const std::string& s, int fallback) {
  try {
    return std::stoi(s);
  } catch (const std::exception&) {
    return fallback;
  }
}

const char* kGenderLabels[] = {"男", "女", "未知"};
const char* kPrivacyLabels[] = {"公开", "亲缘", "私密"};

}  // namespace

bool PersonCardPanel::load(DbSession& session) {
  const std::string& id = state_.selected_person_id;
  if (id.empty()) {
    loaded_id_.clear();
    return false;
  }
  auto got = session.persons().get_person(id);
  if (!got) {
    state_.set_status("读取人物失败: " + got.error().message(), true);
    loaded_id_.clear();
    return false;
  }
  const Person& p = *got;

  buf_family_ = p.family_name;
  buf_given_ = p.given_name;
  buf_courtesy_ = opt_str(p.courtesy_name);
  buf_art_ = opt_str(p.art_name);
  buf_posthumous_ = opt_str(p.posthumous_title);
  buf_alias_ = opt_str(p.alias);
  buf_biography_ = opt_str(p.biography);
  buf_note_ = opt_str(p.note);
  // 长文本同步进常驻缓冲（截断保护：超长按缓冲容量截，丢字比崩好）
  std::snprintf(bio_buf_, sizeof(bio_buf_), "%s", buf_biography_.c_str());
  std::snprintf(note_buf_, sizeof(note_buf_), "%s", buf_note_.c_str());
  // 名号变更史（M7.1）：读失败不拦人物卡（列表留空 + 状态栏提示），
  // 主信息照常编辑——名号是补充维度，不该阻塞主流程
  name_rows_.clear();
  {
    PersonNameRepository names(session.db());
    auto ns = names.list(id);
    if (ns) {
      for (const auto& n : *ns) {
        std::string row = std::string("[") + to_string(n.type) + "] " + n.name;
        if (n.is_primary) row += "（主名）";
        if (n.start_date) row += " 起:" + n.start_date->display();
        if (n.end_date) row += " 止:" + n.end_date->display();
        if (n.note) row += " — " + *n.note;
        name_rows_.push_back(std::move(row));
      }
    } else {
      state_.set_status("读取名号失败: " + ns.error().message(), true);
    }
  }
  buf_birth_ = p.birth_date.gregorian.value_or(std::string{});
  buf_death_ = p.death_date ? p.death_date->gregorian.value_or(std::string{})
                            : std::string{};
  buf_gender_ = p.gender == Gender::Male    ? 0
                : p.gender == Gender::Female ? 1
                                             : 2;
  buf_privacy_ = p.privacy == PrivacyLevel::Public  ? 0
                 : p.privacy == PrivacyLevel::Private ? 2
                                                      : 1;
  buf_heir_ = p.is_heir;
  buf_living_ = p.is_living;
  buf_birth_order_ =
      p.birth_order ? std::to_string(*p.birth_order) : std::string{};
  buf_generation_no_ =
      p.generation_no ? std::to_string(*p.generation_no) : std::string{};

  loaded_id_ = p.id;
  loaded_rev_ = static_cast<int>(p.rev);
  dirty_ = false;

  // 关系链：父母 + 配偶 + 子女（供跳转）
  father_id_ = p.father_id.value_or(std::string{});
  mother_id_ = p.mother_id.value_or(std::string{});
  spouse_ids_.clear();
  child_ids_.clear();
  UnionFilter uf;
  uf.person_id = p.id;
  uf.limit = 100;
  if (auto us = session.unions().list_unions(uf)) {
    for (const auto& u : *us) {
      for (const auto& m : u.members) {
        if (m.person_id != p.id) spouse_ids_.push_back(m.person_id);
      }
    }
  }
  if (!father_id_.empty() || !mother_id_.empty()) {
    // 仓储的 parent_any（father_id 或 mother_id 命中，走索引点查）。
    // 父母都登记时查两次取并集；对父查询带出的「mother_id=父」这类
    // 无意义分支在真实数据里为空，并集语义不受影响。此前是全量装载
    // 20 万人物内存过滤——大库上每次选中都是一次全表遍历。
    std::vector<std::string> seen;
    auto collect = [&](const std::string& parent) {
      if (parent.empty()) return;
      PersonFilter cf;
      cf.parent_any = parent;
      cf.limit = 1000;
      if (auto children = session.persons().list_persons(cf)) {
        for (const auto& c : *children) {
          if (std::find(seen.begin(), seen.end(), c.id) != seen.end()) continue;
          seen.push_back(c.id);
          child_ids_.push_back(c.id);
        }
      }
    };
    collect(father_id_);
    collect(mother_id_);
  }
  return true;
}

bool PersonCardPanel::save(DbSession& session) {
  if (loaded_id_.empty()) return false;
  auto cur = session.persons().get_person(loaded_id_);
  if (!cur) {
    state_.set_status("保存前读取失败: " + cur.error().message(), true);
    return false;
  }
  Person p = *cur;   // 以库中当前值为底，只覆写编辑缓冲覆盖的字段

  p.family_name = buf_family_;
  p.given_name = buf_given_;
  p.courtesy_name = to_opt(buf_courtesy_);
  p.art_name = to_opt(buf_art_);
  p.posthumous_title = to_opt(buf_posthumous_);
  p.alias = to_opt(buf_alias_);
  // 行传/备注以常驻缓冲为准（用户在多行框里编辑的是它）
  buf_biography_ = bio_buf_;
  buf_note_ = note_buf_;
  p.biography = to_opt(buf_biography_);
  p.note = to_opt(buf_note_);
  p.gender = buf_gender_ == 0   ? Gender::Male
              : buf_gender_ == 1 ? Gender::Female
                                : Gender::Unknown;
  p.privacy = buf_privacy_ == 0   ? PrivacyLevel::Public
               : buf_privacy_ == 2 ? PrivacyLevel::Private
                                   : PrivacyLevel::Kin;
  p.is_heir = buf_heir_;
  p.is_living = buf_living_;
  p.birth_order = buf_birth_order_.empty()
                      ? std::optional<int>{}
                      : std::optional<int>{parse_int_or(buf_birth_order_, 0)};
  p.generation_no = buf_generation_no_.empty()
                        ? std::optional<int>{}
                        : std::optional<int>{parse_int_or(buf_generation_no_, 0)};

  // 日期：空串 = 清除；非空按 ISO 解析（yyyy / yyyy-mm / yyyy-mm-dd）。
  if (buf_birth_.empty()) {
    p.birth_date = GenealogyDate{};
  } else if (auto d = GenealogyDate::from_iso(buf_birth_)) {
    p.birth_date = *d;
  } else {
    state_.set_status("出生日期不是合法 ISO（yyyy / yyyy-mm / yyyy-mm-dd）",
                      true);
    return false;
  }
  if (buf_death_.empty()) {
    p.death_date.reset();
  } else if (auto d = GenealogyDate::from_iso(buf_death_)) {
    p.death_date = *d;
  } else {
    state_.set_status("卒年日期不是合法 ISO（yyyy / yyyy-mm / yyyy-mm-dd）",
                      true);
    return false;
  }

  // rev 用「载入时读到的值」：并发编辑会拿到 409，由用户重载后再改。
  // 这比静默覆盖安全——家谱数据不可再生。
  auto saved = session.persons().update_person(p);
  if (!saved) {
    state_.set_status("保存失败: " + saved.error().message(), true);
    return false;
  }
  loaded_rev_ = static_cast<int>(saved->rev);
  dirty_ = false;
  state_.set_status("已保存 " + saved->family_name + saved->given_name +
                    "（rev=" + std::to_string(saved->rev) + "）");
  return true;
}

void PersonCardPanel::draw(DbSession& session) {
  if (!session.is_open()) {
    ImGui::TextDisabled("（未打开数据库）");
    return;
  }
  if (state_.selected_person_id.empty()) {
    ImGui::TextDisabled("（在检索/世系表/世系树里点选一个人物）");
    return;
  }
  // 选中变了就重新装载（跨面板联动的落点）
  if (state_.selected_person_id != loaded_id_) {
    load(session);
    if (loaded_id_.empty()) return;
  }

  ImGui::SeparatorText("姓名");
  ImGui::SetNextItemWidth(120);
  input_text("姓", &buf_family_, 64);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  input_text("名", &buf_given_, 64);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::Combo("性别", &buf_gender_, kGenderLabels, 3);

  ImGui::SetNextItemWidth(140);
  input_text("字", &buf_courtesy_, 64);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  input_text("号", &buf_art_, 64);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  input_text("谥/赠", &buf_posthumous_, 64);

  ImGui::SetNextItemWidth(180);
  input_text("别称/改名前名", &buf_alias_, 96);
  // 名号变更史（M7.1 验收门：人物卡显示全部名号）——只读列表，
  // 增删走 genea person name / REST /persons/{id}/names
  if (!name_rows_.empty()) {
    ImGui::SeparatorText("名号");
    for (const auto& row : name_rows_) {
      ImGui::TextUnformatted(row.c_str());
    }
  }

  ImGui::SeparatorText("生卒");
  ImGui::SetNextItemWidth(120);
  input_text("生（公历）", &buf_birth_, 32);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  input_text("卒（公历）", &buf_death_, 32);
  ImGui::SameLine();
  ImGui::Checkbox("在世", &buf_living_);
  ImGui::SameLine();
  ImGui::Checkbox("承重/嫡嗣", &buf_heir_);

  ImGui::SeparatorText("行辈与隐私");
  ImGui::SetNextItemWidth(80);
  input_text("行几", &buf_birth_order_, 8);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  input_text("世代", &buf_generation_no_, 8);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::Combo("隐私", &buf_privacy_, kPrivacyLabels, 3);

  ImGui::SeparatorText("行传");
  input_text_multiline("##biography", bio_buf_, sizeof(bio_buf_),
                       ImGui::GetTextLineHeight() * 6);

  ImGui::SeparatorText("备注");
  input_text_multiline("##note", note_buf_, sizeof(note_buf_),
                       ImGui::GetTextLineHeight() * 3);

  if (dirty_) {
    ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.2F, 1.0F), "有未保存修改");
    ImGui::SameLine();
  }
  if (ImGui::Button("保存")) {
    save(session);
  }
  ImGui::SameLine();
  if (ImGui::Button("重新载入")) {
    load(session);
  }
  ImGui::SameLine();
  ImGui::Text("rev=%d", loaded_rev_);

  // 关系链：点了就换全局选中，人物卡自己会跟着换
  ImGui::SeparatorText("关系");
  auto link = [&](const char* label, const std::string& id) {
    if (id.empty()) {
      ImGui::Text("%s: —", label);
      return;
    }
    ImGui::PushID(id.c_str());
    if (ImGui::SmallButton(label)) {
      state_.selected_person_id = id;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", id.c_str());
    ImGui::PopID();
  };
  link("父", father_id_);
  link("母", mother_id_);
  for (const auto& id : spouse_ids_) link("配偶", id);
  for (const auto& id : child_ids_) link("子", id);
}

}  // namespace genealogy::gui
