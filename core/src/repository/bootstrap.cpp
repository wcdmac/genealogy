#include <genealogy/repository/bootstrap.hpp>

#include <sqlite3.h>

namespace genealogy {

void ensure_default_lineage(Database& db) {
  sqlite3_exec(db.raw(),
               "INSERT OR IGNORE INTO lineage(id, name, surname,"
               " created_at, updated_at, rev)"
               " VALUES('00000000000000000000000000','默认宗族','',"
               " datetime('now'), datetime('now'), 1);",
               nullptr, nullptr, nullptr);
}

}  // namespace genealogy
