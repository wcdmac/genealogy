#pragma once

// 限流（M8.2，设计文档 5.2.1：令牌桶 + 429 + Retry-After +
// X-RateLimit-Limit/Remaining/Reset）。每客户端（remote addr）一个桶，
// 容量默认 60 req/min（env GENEALOGY_RATE_LIMIT 覆盖）；/health 与
// OPTIONS 豁免（探针与预检不该占配额）。
//
// pre-routing 短路 429；正常响应的 X-RateLimit-* 头在 postHandling
// advice 注入（短路响应自带头——drogon 短路不经过 postHandling，
// 与 401 的 CORS 头处理同因）。

#include <drogon/HttpAppFramework.h>

namespace genealogy::server {

// 注册限流 advice。capacity_per_minute <= 0 时读
// GENEALOGY_RATE_LIMIT（缺省 60）。
void install_rate_limit(drogon::HttpAppFramework& app,
                        int capacity_per_minute = 0);

// 测试钩子：清空所有桶（fixture 跨用例复用进程，桶状态会串扰）。
void reset_rate_limiters();

}  // namespace genealogy::server
