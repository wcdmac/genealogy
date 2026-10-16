#pragma once

// REST 路由：鉴权（/auth/token、/me）。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace genealogy::server {

using Callback =
    std::function<void(const drogon::HttpResponsePtr&)>;

void auth_token(const drogon::HttpRequestPtr& req, Callback&& cb);
void me(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
