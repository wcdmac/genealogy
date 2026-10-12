#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/person_repository.hpp>

namespace genealogy {

// 用例层：与 REST API 的命令一一对应（设计文档 ADR-1：API 优先）。
// 无状态、可重入；线程安全由 Database 的事务边界保证。
class PersonService {
 public:
  explicit PersonService(Database& db);

  // 建档：校验领域不变量（必填、父母存在性）后落库。
  Result<Person> create_person(const Person& draft);

  Result<Person> get_person(const std::string& id);
  Result<std::vector<Person>> list_persons(const PersonFilter& filter);
  Result<std::vector<Person>> search_persons(std::string_view keyword,
                                             int limit);

 private:
  PersonRepository repo_;
};

}  // namespace genealogy
