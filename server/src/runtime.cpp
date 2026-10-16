#include <genealogy/server/runtime.hpp>

#include <mutex>

namespace genealogy::server {

namespace {

// 进程级单例。写只发生在启动时（bind_runtime 先于 app.run）与测试的
// 准备阶段，运行期只读——故无需加锁；用 mutex 保护反而会让每个请求的
// 读路径多付一次原子操作。
Database* g_db = nullptr;
bool g_require_auth = false;

}  // namespace

void bind_runtime(Database& db, const bool require_auth) {
  g_db = &db;
  g_require_auth = require_auth;
}

Database* db_from(const drogon::HttpRequestPtr& req, std::string* why) {
  // 优先走请求上下文：Drogon 的 attributes 按请求存活，
  // 由 main/test 注册的 pre-routing advice 填入。
  if (auto* db = req->getAttributes()->get<Database*>("db"); db != nullptr) {
    return db;
  }
  // 上下文没有就退到进程级单例（pre-routing advice 没跑到的路径）
  if (g_db != nullptr) return g_db;
  if (why != nullptr) *why = "服务未绑定数据库（应在启动时 bind_runtime）";
  return nullptr;
}

bool require_auth_of(const drogon::HttpRequestPtr& req) {
  // Attributes::get<T> 返回 const T&，键缺失时给静态空值（bool → false），
  // 故无需判空；只有显式插入过的请求才会走到插入值。
  const bool from_attr = req->getAttributes()->get<bool>("require_auth");
  // 注意：attributes 里没有键时拿到的是 false，与「显式插入 false」
  // 无法区分——本地模式下二者语义一致（都不要求鉴权），故直接采信，
  // 采不到再退到进程级开关。
  // Attributes::find 返回 bool（不是迭代器），别跟 nullptr 比
  if (req->getAttributes()->find("require_auth")) {
    return from_attr;
  }
  return g_require_auth;
}

}  // namespace genealogy::server
