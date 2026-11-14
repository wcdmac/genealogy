#include <genealogy/repository/bootstrap.hpp>

#include "sqlite_util.hpp"

namespace genealogy {

Result<void> ensure_default_lineage(Database& db) {
  // 占位宗族的 id 是对外公共常量（CLI/GUI 的 --lineage 缺省即它），
  // SQL 字面量必须引用同一来源——此前常量与 SQL 各写一份，改常量会
  // 静默漂移。exec 的返回值也不能丢：库只读/磁盘满时这是唯一报错口。
  const std::string sql =
      std::string("INSERT OR IGNORE INTO lineage(id, name, surname,") +
      " created_at, updated_at, rev)"
      " VALUES('" +
      kDefaultLineageId + "','默认宗族','',"
      " datetime('now'), datetime('now'), 1);";
  return repo_internal::exec(db.raw(), sql.c_str());
}

}  // namespace genealogy
