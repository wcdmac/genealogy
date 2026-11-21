#include <genealogy/server/idempotency.hpp>

#include <sqlite3.h>

#include <iostream>
#include <string>

#include <genealogy/server/auth.hpp>
#include <genealogy/server/routes_auth.hpp>
#include <genealogy/util/time.hpp>
#include <nlohmann/json.hpp>

namespace {
// 复合缓存键：身份 + 端点 + 客户端 key。换行符做分隔（key 本身可能含
// 任意字符，用控制字符保证不产生歧义拼接）。
std::string composite_key(const drogon::HttpRequestPtr& req,
                          const std::string& key) {
  const auto identity = genealogy::server::identity_of(req);
  std::string out = identity.username.empty() ? std::string("local")
                                              : identity.username;
  out.push_back('\n');
  out += req->path();
  out.push_back('\n');
  out += key;
  return out;
}
}  // namespace

namespace genealogy::server {

namespace {

// 裸 prepare：server 不引 core 私有头 sqlite_util（audit.cpp 同款）
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

static std::int64_t unix_now() {
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

std::optional<IdempotentReplay> idempotency_replay(
    Database& db, const drogon::HttpRequestPtr& req, const std::string& key) {
  if (key.empty()) return std::nullopt;
  const std::string composite = composite_key(req, key);
  Stmt st(db.raw(),
          "SELECT status, body FROM idempotency_cache"
          " WHERE key = ?1 AND created_at >= ?2");
  if (!st.ok()) return std::nullopt;
  sqlite3_bind_text(st.get(), 1, composite.c_str(), -1, SQLITE_TRANSIENT);
  // 24h 窗口：Unix 秒数值比较（created_at 存秒）
  sqlite3_bind_int64(st.get(), 2, unix_now() - 24 * 3600);
  if (sqlite3_step(st.get()) == SQLITE_ROW) {
    const auto* status_text =
        reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 0));
    const auto* body_text =
        reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 1));
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(
        status_text ? std::atoi(status_text) : 200));
    resp->setBody(body_text ? body_text : "");
    resp->setContentTypeString("application/json; charset=utf-8");
    resp->addHeader("Idempotency-Replayed", "true");
    // ETag 回放（第三轮新-10）：201 响应带 ETag（rev 乐观锁锚点），
    // 重放曾把它丢掉，客户端拿不到 If-Match 基准。有 rev 的实体在缓存
    // 体里都有 rev 字段，从这里恢复；event/place 等无 rev 实体本就不
    // 设 ETag，保持一致。
    try {
      const auto body_json = nlohmann::json::parse(body_text ? body_text : "");
      if (body_json.is_object() && body_json.contains("rev") &&
          body_json["rev"].is_number()) {
        resp->addHeader(
            "ETag",
            std::string("\"") +
                std::to_string(body_json["rev"].get<std::int64_t>()) + "\"");
      }
    } catch (const std::exception&) {
      // 非 JSON 缓存体（理论不会出现）：不设 ETag
    }
    return IdempotentReplay{resp};
  }
  return std::nullopt;
}

void idempotency_store(Database& db, const drogon::HttpRequestPtr& req,
                       const std::string& key, int status,
                       const std::string& body) {
  if (key.empty()) return;
  const std::string composite = composite_key(req, key);
  if (status < 200 || status >= 300) return;  // 只缓存 2xx
  try {
    {
      // 惰性过期清理：写入顺手删 24h 前条目（Unix 秒数值比较）
      Stmt del(db.raw(),
               "DELETE FROM idempotency_cache WHERE created_at < ?1");
      sqlite3_bind_int64(del.get(), 1, unix_now() - 24 * 3600);
      sqlite3_step(del.get());
    }
    Stmt ins(db.raw(),
             "INSERT OR REPLACE INTO idempotency_cache"
             " (key, status, body, created_at) VALUES (?1, ?2, ?3, ?4)");
    if (!ins.ok()) throw std::runtime_error("准备失败");
    sqlite3_bind_text(ins.get(), 1, composite.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.get(), 2, std::to_string(status).c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.get(), 3, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(ins.get(), 4, unix_now());
    if (sqlite3_step(ins.get()) != SQLITE_DONE) {
      throw std::runtime_error(sqlite3_errmsg(db.raw()));
    }
  } catch (const std::exception& e) {
    // 缓存失败不阻断主写：幂等是便利性保障，主写已成功
    std::cerr << "警告: 幂等缓存写入失败（key " << key << "）: " << e.what()
              << "\n";
  }
}

}  // namespace genealogy::server
