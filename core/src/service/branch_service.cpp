#include <genealogy/service/branch_service.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

BranchService::BranchService(Database& db)
    : db_(&db), repo_(db), lineages_(db), persons_(db) {}

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
  // ROADMAP M7.2：删除被引事实时 409——支派被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  const int n = citations.count_for_subject(CitationSubjectType::Branch, id);
  if (n > 0) {
    return conflict("支派被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  return repo_.remove(id);
}

}  // namespace genealogy
