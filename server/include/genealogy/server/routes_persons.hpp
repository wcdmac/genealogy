#pragma once

// REST 路由：人物 + 世系算法。
//
// 每个函数对应 openapi 里的一组路径，签名统一为
//   void f(const HttpRequestPtr&, Callback&&)
// 其中 Callback = std::function<void(const HttpResponsePtr&)>，
// 由 main.cpp 用 app().registerHandler() 挂到具体路径上。
// 数据库连接由 app 启动时建好、经请求上下文带进来（见 main.cpp），
// 路由函数本身不碰连接生命周期。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace genealogy::server {

using Callback =
    std::function<void(const drogon::HttpResponsePtr&)>;

// ---- 人物 ----
void persons_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void persons_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 世系算法 ----
void person_ancestors(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_descendants(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_tree(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_mourning(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_appellation(const drogon::HttpRequestPtr& req, Callback&& cb);
// 名号（M7.1）：/persons/{id}/names
void person_names_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_names_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void person_name_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
