#pragma once

// REST 路由：基础资源 CRUD（宗族 / 支派 / 婚姻结合 / 事件 / 地点）。
//
// 这一组的形态高度一致：列表（游标分页）+ 建档 + 详情 + 更新 + 删除。
// 逐一展开写而不是套模板——每类的筛选参数与更新语义都有差别，
// 硬套模板会让「哪些字段可改」这种关键信息藏进元编程里。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace genealogy::server {

using Callback =
    std::function<void(const drogon::HttpResponsePtr&)>;

// ---- 宗族 ----
void lineages_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void lineages_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void lineage_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void lineage_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void lineage_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 支派 ----
void branches_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void branches_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void branch_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void branch_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void branch_delete(const drogon::HttpRequestPtr& req, Callback&& cb);
void branch_tree(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 婚姻结合 ----
void unions_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void unions_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void union_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void union_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void union_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 事件 ----
void events_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void events_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void event_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void event_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 地点 ----
void places_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void places_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void place_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void place_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void place_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
