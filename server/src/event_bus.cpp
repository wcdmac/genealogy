#include <genealogy/server/event_bus.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <sstream>

#include <nlohmann/json.hpp>

namespace genealogy::server {

namespace {

std::string now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &seconds);
#else
  gmtime_r(&seconds, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

}  // namespace

EventBus& EventBus::instance() {
  static EventBus bus;
  return bus;
}

void EventBus::install(drogon::HttpAppFramework& app) {
  // 每 15s 一条 SSE 注释帧（冒号开头，客户端按协议忽略）：
  // 空闲连接不被中间代理/浏览器判定超时掐掉；发送失败的连接
  // 也借此被 broadcast 清掉（drogon 不提供断开回调，只能靠这个）。
  app.getLoop()->runEvery(15.0, [this]() { broadcast(": ping\n\n"); });
}

void EventBus::subscribe(std::shared_ptr<drogon::ResponseStream> stream) {
  std::lock_guard<std::mutex> lock(mutex_);
  streams_.push_back(std::move(stream));
}

void EventBus::publish(const std::string& type,
                       const std::string& subject_type,
                       const std::string& subject_id,
                       std::optional<std::int64_t> rev) {
  nlohmann::json payload;
  payload["type"] = type;
  payload["subject_type"] = subject_type;
  payload["subject_id"] = subject_id;
  payload["at"] = now_iso8601();
  if (rev) payload["rev"] = *rev;

  // SSE 帧：id + event + data 三行，空行收尾。id 单调增，
  // 客户端可作断线顺序参考（本地总线不重放，仅作观测）。
  static std::atomic<std::uint64_t> sequence{0};
  const auto id = ++sequence;
  std::ostringstream frame;
  frame << "id: " << id << "\n"
        << "event: change\n"
        << "data: " << payload.dump() << "\n\n";
  broadcast(frame.str());
}

std::size_t EventBus::connection_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return streams_.size();
}

void EventBus::broadcast(const std::string& frame) {
  // 持锁只做发送——send 内部会切回各连接的 event loop，
  // 在锁外等可能死锁；锁内顺序发对本地规模（个位数连接）无压力。
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = streams_.begin(); it != streams_.end();) {
    bool alive = false;
    try {
      alive = (*it)->send(frame);
    } catch (const std::exception& error) {
      // 单条连接发送异常不影响其他连接
      std::fprintf(stderr, "[sse] 发送失败: %s\n", error.what());
    }
    if (alive) {
      ++it;
    } else {
      // 发送失败 = 连接已断，清掉（持的是最后一份强引用，
      //  erase 后 ResponseStream 析构关闭底层流）
      it = streams_.erase(it);
    }
  }
}

}  // namespace genealogy::server
