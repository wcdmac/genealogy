#pragma once

// REST 路由：来源（/sources）与证据引用（/citations）——M7.2 证据链。
//
// 引用按主体过滤（subject_type+subject_id）是主用例（人物卡的证据
// 列表、无证据清单的统计），故 /citations 的 GET 同时接受两个过滤参数。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <genealogy/server/routes_auth.hpp>

namespace genealogy::server {

void sources_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void sources_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void source_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void source_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void source_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

void citations_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void citations_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void citation_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void citation_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void citation_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
