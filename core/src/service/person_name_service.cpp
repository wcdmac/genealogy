#include <genealogy/service/person_name_service.hpp>

#include <string>

#include <genealogy/model/person_name.hpp>
#include <genealogy/repository/person_repository.hpp>
#include <genealogy/repository/transaction.hpp>
#include <genealogy/ulid.hpp>

namespace genealogy {

PersonNameService::PersonNameService(Database& db)
    : db_(&db), repo_(db), persons_(db) {}

Result<PersonName> PersonNameService::create_person_name(
    const PersonName& draft) {
  if (draft.person_id.empty()) return invalid_argument("person_id 必填");
  if (draft.name.empty()) return invalid_argument("名号文本必填");
  auto person = persons_.get(draft.person_id);
  if (!person) return not_found("人物不存在: " + draft.person_id);

  PersonName n = draft;
  n.id = generate_ulid();
  // 主名互斥：置主时清同人多条（partial unique index 在库层二次兜底——
  // 若服务层漏了这条纪律，INSERT 会撞唯一索引直接失败，不会静默双主名）
  if (n.is_primary) {
    // Result<void> 不能隐式转 Result<T>（M7.2 教训）——显式搬运错误码
    if (auto r = repo_.clear_primary(n.person_id, n.id); !r) {
      return Error{r.error().code(), r.error().message()};
    }
  }
  return repo_.insert(n);
}

Result<std::vector<PersonName>> PersonNameService::list_person_names(
    const std::string& person_id) {
  return repo_.list(person_id);
}

Result<PersonName> PersonNameService::get_person_name(const std::string& id) {
  return repo_.get(id);
}

Result<void> PersonNameService::remove_person_name(const std::string& id) {
  return repo_.remove(id);
}

}  // namespace genealogy
