#pragma once

// REST 路由：鉴权（/auth/token、/me）+ 全局鉴权守卫。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace drogon {
class HttpAppFramework;
}

namespace genealogy::server {

using Callback =
    std::function<void(const drogon::HttpResponsePtr&)>;

void auth_token(const drogon::HttpRequestPtr& req, Callback&& cb);
void me(const drogon::HttpRequestPtr& req, Callback&& cb);

// 注册 pre-routing 守卫：--require-auth 模式下，除匿名白名单
// （/auth/token、/health、/openapi.yaml，另放行 CORS 预检 OPTIONS）
// 外，所有端点必须持有效 Bearer 令牌，否则 401 problem+json。
// main 与测试二进制都要注册——鉴权是横切行为，漏注册一处，
// 「--require-auth 必须带令牌」的声明就在那条路径上是假的。
void install_auth_guard(drogon::HttpAppFramework& app);

}  // namespace genealogy::server
