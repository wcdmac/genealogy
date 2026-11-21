#pragma once

#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/repository/lineage_repository.hpp>
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

  // 更新：调用方须传完整实体（含期望 rev），仓储据此做乐观锁比对，
  // 不匹配返回 Conflict（HTTP 409）。领域不变量与建档同一套校验。
  Result<Person> update_person(const Person& p);

  // 删除。注意：当前为硬删除——schema 无 deleted_at/audit_log 表，
  // 规范 5.2.2 的「软删除入 audit_log」待 M3 补表后实现。
  // 此人的事件与迁徙随主体一并删除（subject_id 无外键，不删即悬挂）。
  Result<void> remove_person(const std::string& id);

  Result<std::vector<Person>> list_persons(const PersonFilter& filter);
  Result<std::vector<Person>> search_persons(std::string_view keyword,
                                             int limit,
                                             const std::string& after_id = {});

 private:
  // 事件/迁徙的级联清理需要各自的仓储（remove_person）
  Database* db_ = nullptr;
  PersonRepository repo_;
  LineageRepository lineages_;
};

}  // namespace genealogy
