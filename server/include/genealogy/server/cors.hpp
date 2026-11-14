#pragma once

// CORS：Web 客户端外接的必需设施（设计文档 5.2 接口约定）。
//
// 默认只放行本机来源（localhost / 127.0.0.1 / [::1] 任意端口）——
// 服务绑 127.0.0.1 挡不住受害者浏览器里的第三方网页（drive-by），
// 外部 Origin 一律不给 Access-Control 头，浏览器会拦住跨站读写。
// 公网部署用 GENEALOGY_CORS_ORIGINS="https://a.com,https://b.com"
// 显式白名单（逗号分隔；* = 放开，自担风险）。
//
// 为什么用 advice 而不是每个 handler 自己加：错误响应（problem+json、
// 404 默认 handler）同样要带头，否则浏览器只报一个笼统的 CORS 错误，
// 真正的 status/code 反而看不到——排查全绕远路。
//
// postHandling advice 只覆盖「走完整 routing→handling 路径」的响应；
// pre-routing 短路的响应（鉴权守卫的 401、CORS 自己的预检 204）不经过
// 它——需要带头的地方必须显式调 attach_allow_origin（drogon 的
// HttpServer 对短路响应直接走原始 callback 发送）。

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>

#include <string>

namespace genealogy::server {

void install_cors(drogon::HttpAppFramework& app,
                  const std::string& allowed_origins);

// 该 Origin 是否被当前配置放行（install_cors 之后才有效）。
bool origin_allowed_by_config(const std::string& origin);

// Origin 放行时给响应补 Access-Control-Allow-Origin + Vary。
// 供 postHandling advice 与 pre-routing 短路出口（如 401）共用，
// 判据与主链路完全一致。
void attach_allow_origin(const drogon::HttpRequestPtr& req,
                         const drogon::HttpResponsePtr& resp);

}  // namespace genealogy::server
