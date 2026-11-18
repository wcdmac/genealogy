#include <genealogy/server/rate_limit.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

#include <genealogy/server/cors.hpp>
#include <genealogy/server/problem.hpp>

namespace genealogy::server {

namespace {

struct Bucket {
  double tokens = 0;
  std::chrono::steady_clock::time_point last;
};

struct LimiterState {
  std::mutex mu;
  double capacity = 60;
  bool installed = false;
  std::unordered_map<std::string, Bucket> buckets;
};

LimiterState& state() {
  static LimiterState s;
  return s;
}

std::string client_of(const drogon::HttpRequestPtr& req) {
  // 按 IP 键控（不含端口）：每个 HTTP 连接的客户端端口都是新的临时
  // 端口，带端口键控会让每个连接各得一个满桶——限流永远打不满
  //（M8.2 验收测试实测踩过）。同一 IP 共享配额即设计语义。
  const std::string ip = req->peerAddr().toIp();
  return ip.empty() ? std::string("unknown") : ip;
}

}  // namespace

void install_rate_limit(drogon::HttpAppFramework& app,
                        int capacity_per_minute) {
  auto& st = state();
  double cap = static_cast<double>(capacity_per_minute);
  if (cap <= 0) {
    if (const char* env = std::getenv("GENEALOGY_RATE_LIMIT")) {
      if (*env != '\0') cap = std::atof(env);
    }
  }
  if (cap <= 0) cap = 60;
  st.capacity = cap;
  // advice 只注册一次；容量运行时可调（测试钩子二次 install 只改
  // state().capacity），lambda 每请求现读——值捕获会让运行时改容量
  // 失效（首版实测踩过）。
  if (st.installed) return;
  st.installed = true;

  app.registerPreRoutingAdvice(
      [](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb,
         drogon::AdviceChainCallback&& accb) {
        if (req->method() == drogon::Options) {
          accb();
          return;
        }
        const std::string& path = req->path();
        if (path == "/api/v1/health" || path == "/openapi.yaml") {
          accb();
          return;
        }
        auto& st2 = state();
        const std::string key = client_of(req);
        bool allowed = false;
        int remaining = 0;
        std::int64_t reset_at = 0;
        {
          std::lock_guard<std::mutex> lk(st2.mu);
          const auto now = std::chrono::steady_clock::now();
          // 惰性清扫：每 60s 一次，清掉 10 分钟未触达的桶（键控池
          // 无淘汰会随客户端 IP 数无界增长——2026-11 静态审查 L3）
          static std::chrono::steady_clock::time_point last_sweep{};
          if (last_sweep.time_since_epoch().count() == 0 ||
              now - last_sweep > std::chrono::seconds(60)) {
            last_sweep = now;
            for (auto it = st2.buckets.begin(); it != st2.buckets.end();) {
              if (now - it->second.last > std::chrono::seconds(600)) {
                it = st2.buckets.erase(it);
              } else {
                ++it;
              }
            }
          }
          Bucket& b = st2.buckets[key];
          const double rate = st2.capacity / 60.0;  // 每秒回填
          if (b.last.time_since_epoch().count() == 0) {
            b.tokens = st2.capacity;
            b.last = now;
          } else {
            const std::chrono::duration<double> elapsed = now - b.last;
            b.tokens = (std::min)(st2.capacity,
                                  b.tokens + elapsed.count() * rate);
            b.last = now;
          }
          if (b.tokens >= 1.0) {
            b.tokens -= 1.0;
            allowed = true;
          }
          remaining = static_cast<int>(b.tokens);
          const std::chrono::duration<double> to_full =
              std::chrono::duration<double>(st2.capacity - b.tokens) / rate;
          reset_at = static_cast<std::int64_t>(
              std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count() +
              to_full.count());
        }
        if (!allowed) {
          // Retry-After 按回填速率动态算（恒 1 曾误导重试节奏——
          // 桶空时要等约 capacity/60 秒才有一颗令牌）。
          auto resp = problem_response(429, 42901, "请求过于频繁",
                                       "限流窗口内请求过多，请稍后重试",
                                       req->path());
          const int retry_after = to_full.count() < 1.0
                                      ? 1
                                      : static_cast<int>(to_full.count()) + 1;
          resp->addHeader("Retry-After", std::to_string(retry_after));
          resp->addHeader("X-RateLimit-Limit",
                          std::to_string(static_cast<int>(state().capacity)));
          resp->addHeader("X-RateLimit-Remaining", "0");
          resp->addHeader("X-RateLimit-Reset", std::to_string(reset_at));
          attach_allow_origin(req, resp);
          acb(resp);
          return;
        }
        // Remaining/Reset 记 attributes，postHandling 注入正常响应的头
        req->attributes()->insert("rate_remaining", remaining);
        req->attributes()->insert("rate_reset", reset_at);
        accb();
      });

  // 正常响应补 X-RateLimit-*（429 已在短路时自带）。参数必须
  // const HttpResponsePtr&（非 const 引用绑不上 std::function 形参）。
  app.registerPostHandlingAdvice(
      [](const drogon::HttpRequestPtr& req,
         const drogon::HttpResponsePtr& resp) {
        const auto& attrs = *req->attributes();
        const std::int64_t reset_at = attrs.get<std::int64_t>("rate_reset");
        if (reset_at == 0) return;  // 未过限流器的请求（401 短路等）
        resp->addHeader("X-RateLimit-Limit",
                        std::to_string(static_cast<int>(state().capacity)));
        resp->addHeader("X-RateLimit-Remaining",
                        std::to_string(attrs.get<int>("rate_remaining")));
        resp->addHeader("X-RateLimit-Reset", std::to_string(reset_at));
      });
}

void reset_rate_limiters() {
  std::lock_guard<std::mutex> lk(state().mu);
  state().buckets.clear();
}

}  // namespace genealogy::server
