#include <genealogy/server/audit.hpp>

#include <sqlite3.h>

#include <iostream>
#include <string>
#include <vector>

#include <genealogy/server/auth.hpp>
#include <genealogy/server/routes_auth.hpp>
#include <genealogy/ulid.hpp>
#include <genealogy/util/time.hpp>

#include <sqlite3.h>

namespace genealogy::server {

namespace {

// 裸 prepare：audit.cpp 不引 core 私有头 sqlite_util（server 看不到
// core/src），Stmt 的 ok()/get() 薄封装用十几行复刻
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    rc_ = sqlite3_prepare_v2(db, sql, -1, &st_, nullptr);
  }
  ~Stmt() { if (st_) sqlite3_finalize(st_); }
  bool ok() const { return rc_ == SQLITE_OK; }
  sqlite3_stmt* get() const { return st_; }
 private:
  int rc_ = SQLITE_OK;
  sqlite3_stmt* st_ = nullptr;
};

}  // namespace

nlohmann::json json_diff(const nlohmann::json& before,
                         const nlohmann::json& after) {
  nlohmann::json d = nlohmann::json::object();
  if (!before.is_object() || !after.is_object()) return d;
  // 变化字段并集：旧有新无 = 删除，新有旧无 = 新增，双方都有且不等 = 修改
  for (const auto& [key, after_val] : after.items()) {
    const auto it = before.find(key);
    if (it == before.end()) {
      d[key] = {{"before", nullptr}, {"after", after_val}};
    } else if (*it != after_val) {
      d[key] = {{"before", *it}, {"after", after_val}};
    }
  }
  for (const auto& [key, before_val] : before.items()) {
    if (after.find(key) == after.end()) {
      d[key] = {{"before", before_val}, {"after", nullptr}};
    }
  }
  return d;
}

void audit_write(Database& db, const drogon::HttpRequestPtr& req,
                 const std::string& entity_type, const std::string& entity_id,
                 const std::string& action, const nlohmann::json& diff) {
  const auto identity = identity_of(req);
  const std::string actor = identity.username.empty() ? "local" : identity.username;
  const std::string sql =
      "INSERT INTO audit_log(id, entity_type, entity_id, action, actor, at,"
      " diff) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";
  try {
    Stmt st(db.raw(), sql.c_str());
    if (!st.ok()) throw std::runtime_error("准备失败");
    int i = 1;
    sqlite3_bind_text(st.get(), i++, generate_ulid().c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(st.get(), i++, entity_type.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(st.get(), i++, entity_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.get(), i++, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.get(), i++, actor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st.get(), i++, utc_now_iso().c_str(), -1,
                      SQLITE_TRANSIENT);
    const std::string diff_str = diff.is_null() ? std::string("{}")
                                                : diff.dump();
    sqlite3_bind_text(st.get(), i++, diff_str.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st.get()) != SQLITE_DONE) {
      throw std::runtime_error(sqlite3_errmsg(db.raw()));
    }
  } catch (const std::exception& e) {
    // 观测面失败不阻断主写，但必须让运维看见——绝不静默
    std::cerr << "警告: 审计写入失败（entity " << entity_type << "/"
              << entity_id << " action " << action << "）: " << e.what()
              << "\n";
  }
}

Result<std::vector<AuditRow>> audit_list(Database& db,
                                         const AuditQuery& q) {
  const std::string& entity_type = q.entity_type;
  const std::string& entity_id = q.entity_id;
  const std::string& cursor = q.cursor;
  const int limit = q.limit;
  std::string sql =
      "SELECT id, entity_type, entity_id, action, actor, at, diff"
      " FROM audit_log"
      " WHERE (?1 IS NULL OR entity_type = ?1)"
      "   AND (?2 IS NULL OR entity_id = ?2)"
      "   AND (?3 IS NULL OR id < ?3)"
      " ORDER BY id DESC LIMIT ?4";
  Stmt st(db.raw(), sql.c_str());
  if (!st.ok()) {
    return Error{ErrorCode::Internal,
                 std::string("准备查询失败: ") + sqlite3_errmsg(db.raw())};
  }
  if (entity_type.empty()) {
    sqlite3_bind_null(st.get(), 1);
  } else {
    sqlite3_bind_text(st.get(), 1, entity_type.c_str(), -1, SQLITE_TRANSIENT);
  }
  if (entity_id.empty()) {
    sqlite3_bind_null(st.get(), 2);
  } else {
    sqlite3_bind_text(st.get(), 2, entity_id.c_str(), -1, SQLITE_TRANSIENT);
  }
  if (cursor.empty()) {
    sqlite3_bind_null(st.get(), 3);
  } else {
    sqlite3_bind_text(st.get(), 3, cursor.c_str(), -1, SQLITE_TRANSIENT);
  }
  sqlite3_bind_int(st.get(), 4, limit > 0 ? limit : 50);

  std::vector<AuditRow> out;
  int rc = SQLITE_OK;
  while ((rc = sqlite3_step(st.get())) == SQLITE_ROW) {
    AuditRow r;
    auto col_text = [&](int col) {
      const auto* text =
          reinterpret_cast<const char*>(sqlite3_column_text(st.get(), col));
      return text != nullptr ? std::string(text) : std::string();
    };
    r.id = col_text(0);
    r.entity_type = col_text(1);
    r.entity_id = col_text(2);
    r.action = col_text(3);
    r.actor = col_text(4);
    r.at = col_text(5);
    const std::string diff_str = col_text(6);
    if (!diff_str.empty()) {
      try {
        r.diff = nlohmann::json::parse(diff_str);
      } catch (const std::exception&) {
        r.diff = nlohmann::json::object();
      }
    }
    out.push_back(std::move(r));
  }
  if (rc != SQLITE_DONE) {
    return Error{ErrorCode::Internal,
                 std::string("查询执行失败: ") + sqlite3_errmsg(db.raw())};
  }
  return out;
}

}  // namespace genealogy::server
