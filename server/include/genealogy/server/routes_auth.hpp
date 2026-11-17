#pragma once

// REST 路由：鉴权（/auth/token、/me）+ 全局鉴权守卫。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <genealogy/server/auth.hpp>

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
// 当前请求身份（M8.4：守卫验证后写入 attributes，handler 据此做
// 细粒度判定如 /audit 仅 steward）。本地模式恒为本地 steward。
auth::Claims identity_of(const drogon::HttpRequestPtr& req);

void install_auth_guard(drogon::HttpAppFramework& app);

}  // namespace genealogy::server
