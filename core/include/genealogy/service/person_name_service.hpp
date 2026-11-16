#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person_name.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_name_repository.hpp>
#include <genealogy/repository/person_repository.hpp>

namespace genealogy {

// 名号用例层。create 必填：person_id（须存在）+ name + type。
// 置主（is_primary=true）时清除该人其它主名标记——「此人当前所用之名」
// 全人唯一，partial unique index 在库层二次兜底。
// remove 不做引用检查：person_name 不被任何表外键引用（台账语义）。
class PersonNameService {
 public:
  explicit PersonNameService(Database& db);

  Result<PersonName> create_person_name(const PersonName& draft);
  Result<std::vector<PersonName>> list_person_names(
      const std::string& person_id);
  Result<PersonName> get_person_name(const std::string& id);
  Result<void> remove_person_name(const std::string& id);

 private:
  Database* db_ = nullptr;
  PersonNameRepository repo_;
  PersonRepository persons_;
};

}  // namespace genealogy
