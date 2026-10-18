#include <genealogy/server/cors.hpp>

#include <cstdlib>
#include <string>

namespace genealogy::server {

namespace {

// 判断 Origin 是否命中白名单。allowed 为空或 "*" 表示放开；
// 否则按逗号分隔精确匹配（不做通配，白名单要的就是确定）。
bool origin_allowed(const std::string& origin, const std::string& allowed) {
  if (origin.empty()) return false;
  if (allowed.empty() || allowed == "*") return true;
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

}  // namespace

void install_cors(drogon::HttpAppFramework& app,
                  const std::string& allowed_origins) {
  const std::string allowed = allowed_origins;

  // 预检：OPTIONS 直接短录用 204 + 允许头，不进路由
  app.registerPreRoutingAdvice(
      [allowed](const drogon::HttpRequestPtr& req,
                drogon::AdviceCallback&& acb,
                drogon::AdviceChainCallback&& accb) {
        if (req->method() == drogon::Options) {
          const std::string origin = req->getHeader("Origin");
          auto resp = drogon::HttpResponse::newHttpResponse();
          resp->setStatusCode(drogon::k204NoContent);
          if (origin_allowed(origin, allowed)) {
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
      [allowed](const drogon::HttpRequestPtr& req,
                const drogon::HttpResponsePtr& resp) {
        const std::string origin = req->getHeader("Origin");
        if (origin_allowed(origin, allowed)) {
          resp->addHeader("Access-Control-Allow-Origin", origin);
          resp->addHeader("Vary", "Origin");
        }
      });
}

}  // namespace genealogy::server
