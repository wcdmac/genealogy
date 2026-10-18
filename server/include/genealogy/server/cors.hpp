#pragma once

// CORS：Web 客户端外接的必需设施（设计文档 5.2 接口约定）。
//
// 本地模式默认放开 Origin——服务只绑 127.0.0.1，没有跨站风险，
// 而开发时 vite dev/preview 跑在另一个端口，不放开连不上。
// 公网部署用 GENEALOGY_CORS_ORIGINS="https://a.com,https://b.com"
// 收紧为白名单（逗号分隔；* 或未设 = 放开）。
//
// 为什么用 advice 而不是每个 handler 自己加：错误响应（problem+json、
// 404 默认 handler）同样要带头，否则浏览器只报一个笼统的 CORS 错误，
// 真正的 status/code 反而看不到——排查全绕远路。

#include <drogon/HttpAppFramework.h>

#include <string>

namespace genealogy::server {

void install_cors(drogon::HttpAppFramework& app,
                  const std::string& allowed_origins);

}  // namespace genealogy::server
