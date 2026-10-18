#pragma once

// SSE 变更流路由（M5-3，设计文档 5.4）。
//
// GET /events/stream：text/event-stream 长连接，多端协同的事实源。
// 帧格式见 EventBus::publish（event: change + data: JSON）。

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>

namespace genealogy::server {

using Callback =
    std::function<void(const drogon::HttpResponsePtr&)>;

void stream_events(const drogon::HttpRequestPtr& req, Callback&& cb);

}  // namespace genealogy::server
