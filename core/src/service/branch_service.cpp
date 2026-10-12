#include <genealogy/service/branch_service.hpp>

#include <genealogy/ulid.hpp>

namespace genealogy {

BranchService::BranchService(Database& db)
    : repo_(db), lineages_(db), persons_(db) {}

Error BranchService::validate(const Branch& b) {
  if (b.lineage_id.empty()) return invalid_argument("lineage_id 必填");
  if (b.name.empty()) return invalid_argument("支派名称必填");

  auto lineage = lineages_.get(b.lineage_id);
  if (!lineage) return not_found("宗族不存在: " + b.lineage_id);

  if (b.founder_person_id && !b.founder_person_id->empty()) {
    if (!is_valid_ulid(*b.founder_person_id)) {
      return invalid_argument("founder_person_id 不是合法 ULID: " +
                              *b.founder_person_id);
    }
    auto founder = persons_.get(*b.founder_person_id);
    if (!founder) return not_found("始迁祖不存在: " + *b.founder_person_id);
  }

  if (b.parent_branch_id && !b.parent_branch_id->empty()) {
    if (!is_valid_ulid(*b.parent_branch_id)) {
      return invalid_argument("parent_branch_id 不是合法 ULID: " +
                              *b.parent_branch_id);
    }
    auto parent = repo_.get(*b.parent_branch_id);
    if (!parent) return not_found("父房不存在: " + *b.parent_branch_id);
    // 支派归属：父房必须与本支派同属一个宗族
    if (parent->lineage_id != b.lineage_id) {
      return invalid_argument("父房不属于同一宗族（跨宗族挂接）");
    }
  }
  return {};
}

Result<Branch> BranchService::create_branch(const Branch& draft) {
  if (auto err = validate(draft); err) return err;
  return repo_.insert(draft);
}

Result<Branch> BranchService::get_branch(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Branch>> BranchService::list_branches(
    const BranchFilter& filter) {
  return repo_.list(filter);
}

Result<Branch> BranchService::update_branch(const Branch& branch) {
  if (branch.id.empty()) return invalid_argument("id 必填");
  if (auto err = validate(branch); err) return err;
  return repo_.update(branch);
}

Result<void> BranchService::remove_branch(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
