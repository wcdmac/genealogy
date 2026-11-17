#include <genealogy/server/routes_auth.hpp>

#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <genealogy/server/auth.hpp>
#include <genealogy/server/cors.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>

namespace genealogy::server {


// 当前请求的身份。auth_guard 验证通过后会把 Claims 写进 request
// attributes（"identity"），这里优先读取——handler 与守卫看到同一份
// 身份，避免二次验签。本地免鉴权模式下恒为本地 steward（raw_privacy
// = true，M8.3 脱敏豁免）。
auth::Claims identity_of(const drogon::HttpRequestPtr& req) {
  // attributes 未命中时 get 返回默认 Claims（username 空）→ 走旧回退
  const auto stored = req->attributes()->get<auth::Claims>("identity");
  if (!stored.username.empty()) return stored;
  if (!require_auth_of(req)) return auth::local_identity();
  const std::string h = req->getHeader("authorization");
  if (h.rfind("Bearer ", 0) != 0) return {};
  auth::Claims c;
  std::string err;
  if (!auth::verify_token(h.substr(7), &c, &err)) return {};
  return c;
}

// M8.4 角色 ACL：viewer 禁写（403）、editor 禁删、steward 全权。
// 宏观判定放在守卫层——17 个写 handler 不必各自检查；细粒度
// （如 /audit 仅 steward）在 handler 里用 identity_of 判。
bool method_allowed(drogon::HttpMethod method, auth::Role role) {
  switch (method) {
    case drogon::Get:
    case drogon::Head:
      return true;  // 只读：viewer 及以上
    case drogon::Post:
    case drogon::Patch:
    case drogon::Put:
      return role == auth::Role::Steward || role == auth::Role::Editor;
    case drogon::Delete:
      return role == auth::Role::Steward;
    default:
      return true;  // Options 已在前置分支处理
  }
}

drogon::HttpResponsePtr forbidden(const drogon::HttpRequestPtr& req,
                                  auth::Role role, const std::string& need) {
  auto resp = problem_response(
      403, 4001, "禁止访问",
      "角色 " + std::string(auth::to_string(role)) +
          " 无权执行此操作（需要 " + need + "）",
      req->path());
  attach_allow_origin(req, resp);
  return resp;
}
namespace {

// --require-auth 模式下 401 的统一出口：problem+json + WWW-Authenticate
// + CORS 头。CORS 头不能省：pre-routing 短路的响应不经过 postHandling
// advice（drogon 对短路响应直接走原始 callback 发送），而跨端口本机
// 来源下 401 没有 ACAO 就是浏览器 opaque response——前端分不清
// 「掉线」和「该重新登录」。
drogon::HttpResponsePtr unauthorized(const drogon::HttpRequestPtr& req,
                                     const std::string& detail) {
  auto resp = problem_response(401, 3001, "未认证", detail, req->path());
  resp->addHeader("WWW-Authenticate", "Bearer");
  attach_allow_origin(req, resp);
  return resp;
}

}  // namespace

// POST /auth/token：换取 JWT。
// --require-auth 模式：username/password 必填（与 spec 的 required 一致），
// password 对上 GENEALOGY_AUTH_PASSWORD（启动时经 bind_runtime 注入）
// 才发令牌——口令体系属 M3 的用户模型，在那之前这是唯一的门槛，
// 不能再像早先那样任意空口令发 steward 令牌。
// 本地免鉴权模式：保持便捷通道（任意/空 body 发本地 steward 令牌）。
void auth_token(const drogon::HttpRequestPtr& req, Callback&& cb) {
  const std::string instance = req->path();
  std::string username = "local";
  auth::Role role = auth::Role::Steward;

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req->body());
  } catch (const std::exception&) {
    // 解析不出 JSON：本地模式按空 body 处理；鉴权模式在下面报 400
  }

  if (require_auth_of(req)) {
    if (!body.is_object()) {
      cb(validation_problem(instance, {{"username", "必填"},
                                       {"password", "必填"}},
                            "请求体应为 JSON 对象"));
      return;
    }
    std::vector<FieldError> missing;
    std::string password;
    if (body.contains("username") && body["username"].is_string()) {
      username = body["username"].get<std::string>();
    } else {
      missing.push_back({"username", "必填"});
    }
    if (body.contains("password") && body["password"].is_string()) {
      password = body["password"].get<std::string>();
    } else {
      missing.push_back({"password", "必填"});
    }
    if (!missing.empty()) {
      cb(validation_problem(instance, missing, "参数校验失败"));
      return;
    }
    const std::string& expect = auth_password_config();
    // 口令比对用 ==：口令是启动时注入的单值环境变量，面向本机/局域网
    // 场景；时序侧信道在此威胁模型下不成立（真上云换 M3 用户表时
    // 应换成慢哈希 + 常量时间比较）。
    if (expect.empty() || password != expect) {
      cb(unauthorized(req, "用户名或口令不正确"));
      return;
    }
  } else if (body.is_object() && body.contains("username") &&
             body["username"].is_string()) {
    // 本地模式：username 仍从 body 取（便捷通道，无口令）
    username = body["username"].get<std::string>();
  }

  // M8.4：role 自请与否分模式。require-auth 模式固定签发 steward——
  // 单口令=族尊钥匙，若接受 body.role 自请，viewer 拿到口令就能
  // 自封 steward 击穿 ACL（多用户表属云版用户系统）。本地模式保留
  // body.role（测试便利；本地身份本来就是全权 steward）。
  if (!require_auth_of(req) && body.is_object() &&
      body.contains("role") && body["role"].is_string()) {
    auto r = auth::role_from_string(body["role"].get<std::string>());
    if (!r) {
      cb(problem_response(400, 1001, "参数校验失败",
                          "role 应为 steward / editor / viewer", instance));
      return;
    }
    role = *r;
  }
  const std::string token =
      auth::issue_token(username, role, 24 * 3600,
                        /*raw_privacy=*/!require_auth_of(req));
  nlohmann::json out;
  out["access_token"] = token;
  out["token_type"] = "Bearer";
  out["expires_in"] = 24 * 3600;
  out["role"] = auth::to_string(role);
  cb(json_response(200, out));
}

// GET /me：当前身份与可访问的宗族。
void me(const drogon::HttpRequestPtr& req, Callback&& cb) {
  const auth::Claims c = identity_of(req);
  if (c.username.empty()) {
    cb(problem_response(401, 3001, "未认证",
                        "缺少或无效的 Bearer 令牌", req->path()));
    return;
  }
  nlohmann::json out;
  out["username"] = c.username;
  out["role"] = auth::to_string(c.role);
  // 本地模式面向单库，不按用户切宗族；多租户在 M3
  out["lineage_ids"] = nlohmann::json::array();
  cb(json_response(200, out));
}

void install_auth_guard(drogon::HttpAppFramework& app) {
  // 匿名可达的端点：令牌签发入口（口令在 handler 里校验）、健康探针、
  // OpenAPI 文档。CORS 预检（OPTIONS）不在此处理——install_cors 的
  // pre-routing advice 先注册先跑，OPTIONS 在那里已短路。
  const std::string anonymous[] = {"/api/v1/auth/token", "/api/v1/health",
                                   "/openapi.yaml"};
  app.registerPreRoutingAdvice(
      [anonymous](const drogon::HttpRequestPtr& req,
                  drogon::AdviceCallback&& acb,
                  drogon::AdviceChainCallback&& accb) {
        if (req->method() == drogon::Options) {
          accb();
          return;
        }
        const std::string& path = req->path();
        for (const auto& a : anonymous) {
          if (path == a) {
            accb();
            return;
          }
        }
        if (!require_auth_of(req)) {
          accb();
          return;
        }
        const std::string h = req->getHeader("authorization");
        auth::Claims c;
        std::string err;
        if (h.rfind("Bearer ", 0) != 0 ||
            !auth::verify_token(h.substr(7), &c, &err)) {
          acb(unauthorized(req, "缺少或无效的 Bearer 令牌"));
          return;
        }
        // M8.4：身份下发（handler 经 identity_of 读取）+ 角色 ACL
        req->attributes()->insert("identity", c);
        if (!method_allowed(req->method(), c.role)) {
          const std::string need =
              req->method() == drogon::Delete ? "steward" : "editor 及以上";
          acb(forbidden(req, c.role, need));
          return;
        }
        accb();
      });
}

}  // namespace genealogy::server
