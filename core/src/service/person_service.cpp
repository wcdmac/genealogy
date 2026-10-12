#include <genealogy/service/person_service.hpp>

#include <string>

#include <genealogy/ulid.hpp>

namespace genealogy {

PersonService::PersonService(Database& db) : repo_(db) {}

Result<Person> PersonService::create_person(const Person& draft) {
  if (draft.lineage_id.empty()) return invalid_argument("lineage_id 必填");
  if (draft.family_name.empty()) return invalid_argument("姓必填");
  if (draft.given_name.empty()) return invalid_argument("名必填");

  Person p = draft;
  // 空串 ID 视为未设置（CLI 传 --father "" 等场景），
  // 否则会作为无效外键值写入库中触发 FOREIGN KEY 失败。
  if (p.father_id && p.father_id->empty()) p.father_id.reset();
  if (p.mother_id && p.mother_id->empty()) p.mother_id.reset();
  if (p.branch_id && p.branch_id->empty()) p.branch_id.reset();
  p.id = generate_ulid();

  // 父母存在性（拟制关系在 M1 与 parent_child 对齐后扩展）
  if (p.father_id && !p.father_id->empty()) {
    if (!is_valid_ulid(*p.father_id)) {
      return invalid_argument("father_id 不是合法 ULID: " + *p.father_id);
    }
    auto father = repo_.get(*p.father_id);
    if (!father) return not_found("父亲不存在: " + *p.father_id);
  }
  if (p.mother_id && !p.mother_id->empty()) {
    if (!is_valid_ulid(*p.mother_id)) {
      return invalid_argument("mother_id 不是合法 ULID: " + *p.mother_id);
    }
    auto mother = repo_.get(*p.mother_id);
    if (!mother) return not_found("母亲不存在: " + *p.mother_id);
  }

  return repo_.insert(p);
}

Result<Person> PersonService::get_person(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Person>> PersonService::list_persons(
    const PersonFilter& filter) {
  return repo_.list(filter);
}

Result<std::vector<Person>> PersonService::search_persons(
    std::string_view keyword, int limit) {
  return repo_.search(keyword, limit);
}

}  // namespace genealogy
