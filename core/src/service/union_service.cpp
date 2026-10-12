#include <genealogy/service/union_service.hpp>

#include <genealogy/ulid.hpp>

namespace genealogy {

UnionService::UnionService(Database& db)
    : repo_(db), persons_(db), places_(db) {}

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
  }
  return {};
}

Result<Union> UnionService::create_union(const Union& draft) {
  if (auto err = validate(draft); err) return err;
  return repo_.insert(draft);
}

Result<Union> UnionService::get_union(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Union>> UnionService::list_unions(
    const UnionFilter& filter) {
  return repo_.list(filter);
}

Result<Union> UnionService::update_union(const Union& u) {
  if (u.id.empty()) return invalid_argument("id 必填");
  if (auto err = validate(u); err) return err;
  return repo_.update(u);
}

Result<void> UnionService::remove_union(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
