#pragma once

// REST 路由：检索 / 统计 / 校验 / 导入导出 / 运维。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace genealogy::server {

using Callback =
    std::function<void(const drogon::HttpResponsePtr&)>;

// ---- 检索与统计 ----
void search(const drogon::HttpRequestPtr& req, Callback&& cb);
void stats(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 校验 ----
void validate_run(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 导入导出 ----
void import_gedcom(const drogon::HttpRequestPtr& req, Callback&& cb);
void export_gedcom(const drogon::HttpRequestPtr& req, Callback&& cb);

// 审计查询（M8.5）：GET /audit，steward-only
void audit_query(const drogon::HttpRequestPtr& req, Callback&& cb);
void export_tree(const drogon::HttpRequestPtr& req, Callback&& cb);

// ---- 运维 ----
void health(const drogon::HttpRequestPtr& req, Callback&& cb);
void openapi_doc(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
