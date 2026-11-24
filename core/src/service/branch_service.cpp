#include <genealogy/repository/transaction.hpp>
#include <genealogy/service/branch_service.hpp>

#include <genealogy/ulid.hpp>
#include <genealogy/model/citation.hpp>
#include <genealogy/repository/citation_repository.hpp>

namespace genealogy {

BranchService::BranchService(Database& db)
    : db_(&db), repo_(db), persons_(db) {}

Error BranchService::validate(const Branch& b) {
  if (b.name.empty()) return invalid_argument("支派名称必填");


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
  }
  return {};
}

Result<Branch> BranchService::create_branch(const Branch& draft) {
  Branch normalized = draft;
  if (normalized.founder_person_id && normalized.founder_person_id->empty())
    normalized.founder_person_id.reset();
  if (normalized.parent_branch_id && normalized.parent_branch_id->empty())
    normalized.parent_branch_id.reset();
  if (auto err = validate(normalized); err) return err;
  return repo_.insert(normalized);
}

Result<Branch> BranchService::get_branch(const std::string& id) {
  return repo_.get(id);
}

Result<std::vector<Branch>> BranchService::list_branches(
    const BranchFilter& filter) {
  return repo_.list(filter);
}

Result<Branch> BranchService::update_branch(const Branch& draft) {
  Branch normalized = draft;
  if (normalized.founder_person_id && normalized.founder_person_id->empty())
    normalized.founder_person_id.reset();
  if (normalized.parent_branch_id && normalized.parent_branch_id->empty())
    normalized.parent_branch_id.reset();
  const Branch& branch = normalized;
  if (branch.id.empty()) return invalid_argument("id 必填");
  if (auto err = validate(branch); err) return err;
  // 父子链环检测（第三轮新-1）：A→B、B→A 各自都能过上面的单步校验，
  // 但 GET /branches/{id}/tree 的递归展开会栈溢出。沿父链向上走，
  // 任何一步回到本支派即成环。
  if (branch.parent_branch_id && !branch.parent_branch_id->empty()) {
    std::string cursor = *branch.parent_branch_id;
    for (int guard = 0; guard < 10000; ++guard) {
      if (cursor == branch.id) {
        return invalid_argument("支派父子链成环（" + branch.id + " 的祖先链回到自身）");
      }
      auto cur = repo_.get(cursor);
      if (!cur) break;  // 链尾断链不在此处报错（validate 已保证存在性）
      if (!cur->parent_branch_id || cur->parent_branch_id->empty()) break;
      cursor = *cur->parent_branch_id;
    }
  }
  return repo_.update(branch);
}

Result<void> BranchService::remove_branch(const std::string& id) {
  // 预检与删除必须在同一写事务内，否则并发插入引用会留下孤儿（TOCTOU）。
  Transaction tx(*db_);
  if (tx.error()) return tx.error().value();
  // ROADMAP M7.2：删除被引事实时 409——支派被证据引用则拒绝，
  // 引用解除（删 citation）后即可删除。
  CitationRepository citations(*db_);
  auto cn = citations.count_for_subject(CitationSubjectType::Branch, id);
  if (!cn) return cn.error();
  const int n = *cn;
  if (n > 0) {
    return conflict("支派被 " + std::to_string(n) +
                   " 条证据引用，先解除引用再删除");
  }
  auto removed = repo_.remove(id);
  if (!removed) return removed.error();
  if (auto c = tx.commit(); !c) return c.error();
  return {};
}

}  // namespace genealogy
