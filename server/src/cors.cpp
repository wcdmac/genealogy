#include <genealogy/server/cors.hpp>

#include <cstdlib>
#include <string>

namespace genealogy::server {

namespace {

// Origin 的 host 部分是否为本机（localhost / 127.0.0.1 / [::1]）。
// 只支持 http/https——Origin 头本来就只有这两个 scheme（null 除外，
// 那种直接不放行）。
bool origin_host_is_local(const std::string& origin) {
  const std::size_t scheme = origin.find("://");
  if (scheme == std::string::npos) return false;
  const std::string s = origin.substr(0, scheme);
  if (s != "http" && s != "https") return false;
  std::string host = origin.substr(scheme + 3);
  // 去掉端口。IPv6 字面量带方括号（http://[::1]:8080），端口在 ']' 之后。
  const std::size_t bracket = host.find(']');
  if (bracket != std::string::npos) {
    host = host.substr(0, bracket + 1);
  } else {
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
  }
  return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

// 判断 Origin 是否命中白名单。allowed 为空 → 默认策略：只放行本机来源。
// 依据：服务虽绑 127.0.0.1，但跨站攻击来自受害者浏览器里的网页——
// 任意第三方页面对 http://127.0.0.1:8080 的 fetch 同样带着
// Origin: http://evil.example，默认全放开等于允许 drive-by 读写本机库。
// 本机来源（跨端口开发，如 Vite 5173 → 8080）仍默认可用。
// "*" 表示显式放开；否则按逗号分隔精确匹配（不做通配，白名单要的就是确定）。
bool origin_allowed(const std::string& origin, const std::string& allowed) {
  if (origin.empty()) return false;
  if (allowed.empty()) return origin_host_is_local(origin);
  if (allowed == "*") return true;
  std::size_t start = 0;
  while (start < allowed.size()) {
    const std::size_t comma = allowed.find(',', start);
    const std::size_t end = comma == std::string::npos ? allowed.size() : comma;
    std::string item = allowed.substr(start, end - start);
    // 去掉配置里可能带的首尾空格
    while (!item.empty() && item.back() == ' ') item.pop_back();
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    if (item == origin) return true;
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return false;
}

// install_cors 注入的配置快照。写只发生在启动时（先于 app.run），
// 运行期只读，无需加锁。
std::string g_allowed_origins;

}  // namespace

bool origin_allowed_by_config(const std::string& origin) {
  return origin_allowed(origin, g_allowed_origins);
}

void attach_allow_origin(const drogon::HttpRequestPtr& req,
                         const drogon::HttpResponsePtr& resp) {
  const std::string origin = req->getHeader("Origin");
  if (origin_allowed(origin, g_allowed_origins)) {
    resp->addHeader("Access-Control-Allow-Origin", origin);
    resp->addHeader("Vary", "Origin");
  }
}

void install_cors(drogon::HttpAppFramework& app,
                  const std::string& allowed_origins) {
  g_allowed_origins = allowed_origins;

  // 预检：OPTIONS 直接短录用 204 + 允许头，不进路由
  app.registerPreRoutingAdvice(
      [](const drogon::HttpRequestPtr& req,
         drogon::AdviceCallback&& acb,
         drogon::AdviceChainCallback&& accb) {
        if (req->method() == drogon::Options) {
          const std::string origin = req->getHeader("Origin");
          auto resp = drogon::HttpResponse::newHttpResponse();
          resp->setStatusCode(drogon::k204NoContent);
          if (origin_allowed_by_config(origin)) {
            resp->addHeader("Access-Control-Allow-Origin", origin);
            resp->addHeader("Vary", "Origin");
            resp->addHeader("Access-Control-Allow-Methods",
                            "GET, POST, PATCH, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers",
                            "Content-Type, Authorization, If-Match, "
                            "Idempotency-Key");
            resp->addHeader("Access-Control-Max-Age", "600");
          }
          acb(resp);
          return;
        }
        accb();
      });

  // 实际响应：统一补 Access-Control-Allow-Origin。必须用
  // PostHandlingAdvice——它在 handler 执行后、响应发出前跑，拿得到
  // 响应对象改头。（syncAdvice 是 handler 前的同步预处理、可短路；
  // postRoutingAdvice 拿不到响应。三者语义不同，别混。）
  app.registerPostHandlingAdvice(
      [](const drogon::HttpRequestPtr& req,
         const drogon::HttpResponsePtr& resp) {
        attach_allow_origin(req, resp);
      });
}

}  // namespace genealogy::server
