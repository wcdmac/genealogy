#pragma once

#include <optional>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/genealogy_date.hpp>
#include <genealogy/model/person_name.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy {

class PersonNameRepository {
 public:
  explicit PersonNameRepository(Database& db);

  // 某人的名号列表：主名在前，其余按 id（录入序）稳定排列
  Result<std::vector<PersonName>> list(const std::string& person_id) const;
  Result<PersonName> get(const std::string& id) const;
  Result<PersonName> insert(const PersonName& name);
  Result<void> remove(const std::string& id);
  // 置主名互斥：清除该人其它 is_primary=1 的条目（服务层配对使用）
  Result<void> clear_primary(const std::string& person_id,
                             const std::string& except_id);

 private:
  Database& db_;
};

}  // namespace genealogy
