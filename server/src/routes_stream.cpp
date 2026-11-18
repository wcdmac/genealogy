#include <genealogy/server/routes_stream.hpp>

#include <memory>
#include <string>

#include <genealogy/server/event_bus.hpp>
#include <genealogy/server/problem.hpp>

namespace genealogy::server {

void stream_events(const drogon::HttpRequestPtr& req, Callback&& cb) {
  // 订阅上限（2026-11 静态审查 M12）：超限拒绝，不无限堆积
  if (EventBus::instance().connection_count() >= EventBus::kMaxStreams) {
    cb(problem_response(503, 9002, "订阅数已达上限",
                        "SSE 订阅数已达上限，请稍后重试", req->path()));
    return;
  }
  // 异步流式响应：回调拿到 ResponseStream 的所有权（unique_ptr），
  // 转交 EventBus 持强引用保活；断开由总线发送失败时清理。
  // 为什么不用同步版 newStreamResponse：那个的 callback 是
  // 「拉数据」语义（返回 0 表示结束），适合文件下载；SSE 是
  // 「推数据」语义，必须用异步版。
  auto resp = drogon::HttpResponse::newAsyncStreamResponse(
      [](drogon::ResponseStreamPtr stream) {
        auto shared = std::shared_ptr<drogon::ResponseStream>(std::move(stream));
        // 初始帧：retry 指示断线重连间隔，注释帧宣告连接就绪。
        // 长度用 sizeof-1——手写数字截断过（"retry: 3000\n\n" 是 13 不是 12）。
        const char retry[] = "retry: 3000\n\n";
        const char hello[] = ": connected\n\n";
        if (!shared->send(std::string(retry, sizeof(retry) - 1)) ||
            !shared->send(std::string(hello, sizeof(hello) - 1))) {
          return;  // 秒断的连接：不登记，让 unique_ptr 析构清理
        }
        EventBus::instance().subscribe(std::move(shared));
      },
      true);  // disableKickoffTimeout：SSE 是长连接，禁掉默认踢闲置
  resp->setContentTypeString("text/event-stream; charset=utf-8");
  // 禁缓存：协同语义要求「现在」的事件，代理缓存一秒就过期了
  resp->addHeader("Cache-Control", "no-cache");
  // CORS 响应头由 PostHandlingAdvice 统一补（vite dev 跨端口直连）
  cb(resp);
}

}  // namespace genealogy::server
