#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/repository/branch_repository.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_repository.hpp>

namespace genealogy {

// 支派用例层。校验：宗族必须存在；始迁祖（人）必须存在；
// 父房必须存在且与支派同属一个宗族（支派归属）。
class BranchService {
 public:
  explicit BranchService(Database& db);

  Result<Branch> create_branch(const Branch& draft);
  Result<Branch> get_branch(const std::string& id);
  Result<std::vector<Branch>> list_branches(const BranchFilter& filter);
  Result<Branch> update_branch(const Branch& branch);
  Result<void> remove_branch(const std::string& id);

 private:
  // 引用保护计数需要 CitationRepository（remove）
  Database* db_ = nullptr;
  // 建档/更新共用的领域校验，返回空 Error 表示通过。
  Error validate(const Branch& b);

  BranchRepository repo_;
  PersonRepository persons_;
};

}  // namespace genealogy
