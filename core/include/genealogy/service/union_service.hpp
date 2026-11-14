#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/place_repository.hpp>
#include <genealogy/repository/union_repository.hpp>

namespace genealogy {

// 婚姻结合用例层。校验：至少一名成员；成员不得重复；每个成员
// （人物）必须存在；结合地点若填写必须存在。
class UnionService {
 public:
  explicit UnionService(Database& db);

  Result<Union> create_union(const Union& draft);
  Result<Union> get_union(const std::string& id);
  Result<std::vector<Union>> list_unions(const UnionFilter& filter);
  Result<Union> update_union(const Union& u);
  Result<void> remove_union(const std::string& id);

 private:
  // 引用保护计数需要 CitationRepository（remove）
  Database* db_ = nullptr;
  Error validate(const Union& u);

  UnionRepository repo_;
  PersonRepository persons_;
  PlaceRepository places_;
};

}  // namespace genealogy
