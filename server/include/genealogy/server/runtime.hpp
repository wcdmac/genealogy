#pragma once

// 进程级运行时上下文：数据库句柄与鉴权开关。
//
// 为什么走单例而不是每个请求开连接：SQLite 连接跨请求复用靠 WAL 的
// 多读单写，各请求自身是短事务；一个进程一个连接既省掉反复开关库的
// 开销，也让「库打不开就别起服务」这个fail-fast 判断有地方落。
//
// 路由函数一律经 db_from() 取用，不自己持有连接——这样测试可以在
// 不起 main() 的情况下把临时库绑上来。

#include <string>

#include <drogon/HttpRequest.h>

#include <genealogy/repository/database.hpp>

namespace genealogy::server {

// 启动时调用一次。重复调用以最后一次为准（测试用）。
// auth_password：--require-auth 模式下 /auth/token 的口令
// （来自 GENEALOGY_AUTH_PASSWORD，main 在启动时空值即拒绝启动）。
void bind_runtime(Database& db, bool require_auth,
                  const std::string& auth_password = "");

// 从请求上下文取数据库。取不到说明没调 bind_runtime，
// 属于服务端缺陷，返回 nullptr 并由调用方回 500。
Database* db_from(const drogon::HttpRequestPtr& req, std::string* why);

// 本次请求是否要求鉴权。
bool require_auth_of(const drogon::HttpRequestPtr& req);

// --require-auth 模式下的口令（未配置为空串，auth_token 据此拒发）。
const std::string& auth_password_config();

}  // namespace genealogy::server
