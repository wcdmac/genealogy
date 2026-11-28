// 人物卡面板的无 UI 状态机（ROADMAP「面板装配抽离 ImGui TU」债务偿还）。
//
// load/save/切换决策是纯数据装配——不碰任何 ImGui 符号，单独成 TU 后
// gui-tests 可以无头驱动（第三轮新-C3 修复的 dirty 状态机自此可自动
// 验证）。draw() 留在 person_card.cpp（渲染层），只调用这里的 API。

#include <genealogy/gui/panels/person_card.hpp>

#include <genealogy/model/person.hpp>
#include <genealogy/repository/person_name_repository.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/union_repository.hpp>
#include <genealogy/ulid.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace genealogy::gui {

namespace {

// optional<string> ↔ 输入缓冲的桥接。空串表示「未设置」——与 core 的
// 口径一致（service 层会把空串规范化为未设置）。
std::string opt_str(const std::optional<std::string>& v) {
  return v.value_or(std::string{});
}

std::optional<std::string> to_opt(const std::string& s) {
  if (s.empty()) return std::nullopt;
  return s;
}

int parse_int_or(const std::string& s, int fallback) {
  // 第五轮 低-3：stoi 部分匹配（"12abc"→12）会把脏输入静默存库
  try {
    std::size_t used = 0;
    const int v = std::stoi(s, &used);
    return used == s.size() ? v : fallback;
  } catch (const std::exception&) {
    return fallback;
  }
}

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
    // 失败 id 记档：合并 loser 后选中悬空 id 会每帧重查库+刷状态栏
    // （第四轮 P3）。重选其他人物或换库时清档。
    failed_load_id_ = id;
    return false;
  }
  failed_load_id_.clear();
  const Person& p = *got;

  buf_family_ = p.family_name;
  buf_given_ = p.given_name;
  buf_courtesy_ = opt_str(p.courtesy_name);
  buf_art_ = opt_str(p.art_name);
  buf_posthumous_ = opt_str(p.posthumous_title);
  buf_alias_ = opt_str(p.alias);
  buf_biography_ = opt_str(p.biography);
  buf_note_ = opt_str(p.note);
  // 长文本进 string 缓冲（CallbackResize 桥接后无截断——第三轮新-C14
  // 之前是定长数组 snprintf 截断，丢字比崩好已无必要）
  bio_buf_ = buf_biography_;
  note_buf_ = buf_note_;
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
  if (loaded_id_.empty()) {
    // 读取失败后的"保存"此前静默无效（第五轮 P3-6），给一句人话
    state_.set_status("当前没有可保存的人物（读取失败或未载入）", true);
    return false;
  }
  auto cur = session.persons().get_person(loaded_id_);
  if (!cur) {
    state_.set_status("保存前读取失败: " + cur.error().message(), true);
    return false;
  }
  Person p = *cur;   // 以库中当前值为底，只覆写编辑缓冲覆盖的字段
  // rev 用「载入时读到的值」——此前取当前库值，乐观锁形同虚设：
  // 多端并发编辑会静默互相覆盖（2026-11 静态审查 H11）。旧值会撞
  // 409 冲突，由用户重载后再改——家谱数据不可再生，宁拦勿盖。
  p.rev = loaded_rev_;

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
  // 行几/世代：非法输入报错拒绝（第四轮 P3）——旧行为 parse_int_or 静默
  // 归 0 落库，脏输入变成合法值。与 person_table_state 同口径。
  auto int_field = [&](const char* label, const std::string& buf,
                       std::optional<int>& out) -> bool {
    if (buf.empty()) {
      out.reset();
      return true;
    }
    try {
      std::size_t used = 0;
      const int v = std::stoi(buf, &used);
      if (used != buf.size()) {
        state_.set_status(std::string(label) + "应为整数: " + buf, true);
        return false;
      }
      out = v;
      return true;
    } catch (const std::exception&) {
      state_.set_status(std::string(label) + "应为整数: " + buf, true);
      return false;
    }
  };
  if (!int_field("行几", buf_birth_order_, p.birth_order)) return false;
  if (!int_field("世代", buf_generation_no_, p.generation_no)) return false;

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

void PersonCardPanel::resolve_switch(SwitchAction action, DbSession& session) {
  switch (action) {
    case SwitchAction::Stay:
      // 全局选中退回本卡当前人物——面板与全局选中重新对齐
      state_.selected_person_id = loaded_id_;
      return;
    case SwitchAction::DiscardAndLoad:
      // 缓冲直接作废，按全局选中新值重载
      load(session);
      return;
    case SwitchAction::SaveAndLoad:
      // 先保存；失败（如 409 乐观锁冲突）原地不动，用户看到状态栏
      // 错误后再决定——绝不静默丢编辑
      if (save(session)) load(session);
      return;
  }
}

}  // namespace genealogy::gui
