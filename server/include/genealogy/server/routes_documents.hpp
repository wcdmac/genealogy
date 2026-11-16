#pragma once

// REST 路由：文档（/documents）——M7.4 谱序/凡例/族规家训/艺文/契约。
//
// 正文检索走 GET /search?scope=document（/search 的统一入口，routes_misc）；
// 本文件的 GET /documents 只按 type/lineage 过滤与翻页。
// PATCH 语义与 sources 不同：merge（absent=保留）——document.content
// 是大段正文，PATCH 改标题误清正文不可接受。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <genealogy/server/routes_auth.hpp>

namespace genealogy::server {

void documents_list(const drogon::HttpRequestPtr& req, Callback&& cb);
void documents_create(const drogon::HttpRequestPtr& req, Callback&& cb);
void document_get(const drogon::HttpRequestPtr& req, Callback&& cb);
void document_patch(const drogon::HttpRequestPtr& req, Callback&& cb);
void document_delete(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
