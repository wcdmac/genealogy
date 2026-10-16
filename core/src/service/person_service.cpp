#include <genealogy/service/person_service.hpp>

#include <string>

#include <genealogy/ulid.hpp>

namespace genealogy {

PersonService::PersonService(Database& db) : repo_(db), lineages_(db) {}

namespace {

// 建档与更新共用的领域校验。空 Error 表示通过。
Error validate_person(const Person& draft, LineageRepository& lineages,
                      PersonRepository& repo) {
  if (draft.lineage_id.empty()) return invalid_argument("lineage_id 必填");
  if (draft.family_name.empty()) return invalid_argument("姓必填");
  if (draft.given_name.empty()) return invalid_argument("名必填");

  // 宗族存在性：未传 --lineage 时 CLI 落入占位宗族（bootstrap 保证存在）
  auto lineage = lineages.get(draft.lineage_id);
  if (!lineage) return not_found("宗族不存在: " + draft.lineage_id);

  Person p = draft;
  // 空串 ID 视为未设置（CLI 传 --father "" 等场景），
  // 否则会作为无效外键值写入库中触发 FOREIGN KEY 失败。
  if (p.father_id && p.father_id->empty()) p.father_id.reset();
  if (p.mother_id && p.mother_id->empty()) p.mother_id.reset();
  if (p.branch_id && p.branch_id->empty()) p.branch_id.reset();

  // 父母存在性（拟制关系在 M1 与 parent_child 对齐后扩展）
  if (p.father_id && !p.father_id->empty()) {
    if (!is_valid_ulid(*p.father_id)) {
      return invalid_argument("father_id 不是合法 ULID: " + *p.father_id);
    }
    auto father = repo.get(*p.father_id);
    if (!father) return not_found("父亲不存在: " + *p.father_id);
  }
  if (p.mother_id && !p.mother_id->empty()) {
    if (!is_valid_ulid(*p.mother_id)) {
      return invalid_argument("mother_id 不是合法 ULID: " + *p.mother_id);
    }
    auto mother = repo.get(*p.mother_id);
    if (!mother) return not_found("母亲不存在: " + *p.mother_id);
  }
  return {};
}

}  // namespace

Result<Person> PersonService::create_person(const Person& draft) {
  if (auto err = validate_person(draft, lineages_, repo_); err) return err;
  return repo_.insert(draft);
}

Result<Person> PersonService::get_person(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Person>> PersonService::list_persons(
    const PersonFilter& filter) {
  return repo_.list(filter);
}

Result<Person> PersonService::update_person(const Person& p) {
  if (auto err = validate_person(p, lineages_, repo_); err) return err;
  return repo_.update(p);
}

Result<void> PersonService::remove_person(const std::string& id) {
  // 有人以此人为父母时，外键会拦住删除（SQLite foreign_keys=ON）。
  // 这里是有意的：静默级联删掉一整枝子孙，比明确报错危险得多。
  return repo_.remove(id);
}

Result<std::vector<Person>> PersonService::search_persons(
    std::string_view keyword, int limit) {
  return repo_.search(keyword, limit);
}

}  // namespace genealogy
