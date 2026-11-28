#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/union_service.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>
#include <genealogy/repository/event_repository.hpp>

namespace genealogy {

UnionService::UnionService(Database& db)
    : db_(&db), repo_(db), persons_(db), places_(db) {}

Error UnionService::validate(const Union& u) {
  if (u.members.empty()) return invalid_argument("结合至少需要一名成员");
  if (u.place_id && !u.place_id->empty()) {
    auto place = places_.get(*u.place_id);
    if (!place) return not_found("结合地点不存在: " + *u.place_id);
  }

  std::vector<std::string> seen;
  seen.reserve(u.members.size());
  for (const auto& m : u.members) {
    if (m.person_id.empty()) return invalid_argument("成员 person_id 必填");
    if (!is_valid_ulid(m.person_id)) {
      return invalid_argument("成员 person_id 不是合法 ULID: " + m.person_id);
    }
    for (const auto& s : seen) {
      if (s == m.person_id) {
        return invalid_argument("同一人物在结合中重复出现: " + m.person_id);
      }
    }
    seen.push_back(m.person_id);
    auto person = persons_.get(m.person_id);
    if (!person) return not_found("结合成员不存在: " + m.person_id);
    // 婚姻角色与性别一致性（第四轮 P2）：夫=女/妻=男会让称谓与五服
    // 方向错。性别未知放行（录谱常见），已知则校验。
    if (u.type == UnionType::Marriage) {
      const bool role_husband = m.role == MemberRole::Husband;
      if (person->gender == Gender::Female && role_husband) {
        return invalid_argument("婚姻成员性别为女但角色为夫: " + m.person_id);
      }
      if (person->gender == Gender::Male && !role_husband) {
        return invalid_argument("婚姻成员性别为男但角色为妻: " + m.person_id);
      }
    }
  }
  return {};
}

Result<Union> UnionService::create_union(const Union& draft) {
  Union normalized = draft;
  if (normalized.place_id && normalized.place_id->empty())
    normalized.place_id.reset();
  if (auto err = validate(normalized); err) return err;
  return repo_.insert(normalized);
}

Result<Union> UnionService::get_union(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Union>> UnionService::list_unions(
    const UnionFilter& filter) {
  return repo_.list(filter);
}

Result<Union> UnionService::update_union(const Union& draft) {
  Union normalized = draft;
  if (normalized.place_id && normalized.place_id->empty())
    normalized.place_id.reset();
  if (normalized.id.empty()) return invalid_argument("id 必填");
  if (auto err = validate(normalized); err) return err;
  return repo_.update(normalized);
}

Result<void> UnionService::remove_union(const std::string& id) {
  // 预检与删除必须在同一写事务内，否则并发插入引用会留下孤儿（TOCTOU）。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  // ROADMAP M7.2：删除被引事实时 409——结合被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  auto cn = citations.count_for_subject(CitationSubjectType::Union, id);
  if (!cn) return cn.error();
  const int n = *cn;
  if (n > 0) {
    return conflict("结合被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  // 以结合为主体的事件随结合删除（第四轮 P2-6）：event.subject_id 无
  // 外键，不清理则永久悬挂（person 路径同口径）。
  {
    EventRepository events(*db_);
    std::optional<std::string> ev_cursor;
    for (;;) {
      EventFilter ef;
      ef.subject_type = EventSubjectType::Union;
      ef.subject_id = id;
      ef.limit = 1000;
      ef.cursor = ev_cursor;
      auto evs = events.list(ef);
      if (!evs) return evs.error();
      if (evs->empty()) break;
      for (const auto& e : *evs) {
        if (auto r = events.remove(e.id); !r) return r;
      }
      ev_cursor = evs->back().id;
    }
  }
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
