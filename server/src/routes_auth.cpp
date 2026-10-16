#include <genealogy/server/routes_auth.hpp>

#include <string>

#include <nlohmann/json.hpp>

#include <genealogy/server/auth.hpp>
#include <genealogy/server/dto.hpp>
#include <genealogy/server/problem.hpp>
#include <genealogy/server/runtime.hpp>

namespace genealogy::server {

namespace {

// 当前请求的身份。本地免鉴权模式下恒为本地 steward；
// --require-auth 时从 Authorization: Bearer <jwt> 解出。
auth::Claims identity_of(const drogon::HttpRequestPtr& req) {
  if (!require_auth_of(req)) return auth::local_identity();
  const std::string h = req->getHeader("authorization");
  if (h.rfind("Bearer ", 0) != 0) return {};
  auth::Claims c;
  std::string err;
  if (!auth::verify_token(h.substr(7), &c, &err)) return {};
  return c;
}

}  // namespace

// POST /auth/token：换取 JWT。
// 本地模式下这是一条便捷通道，不校验口令——口令体系属 M3 的用户模型，
// 那时再接 username/password 查表。
void auth_token(const drogon::HttpRequestPtr& req, Callback&& cb) {
  const std::string instance = req->path();
  std::string username = "local";
  auth::Role role = auth::Role::Steward;
  try {
    const auto body = nlohmann::json::parse(req->body());
    if (body.contains("username") && body["username"].is_string()) {
      username = body["username"].get<std::string>();
    }
    if (body.contains("role") && body["role"].is_string()) {
      auto r = auth::role_from_string(body["role"].get<std::string>());
      if (!r) {
        cb(problem_response(400, 1001, "参数校验失败",
                            "role 应为 steward / editor / viewer", instance));
        return;
      }
      role = *r;
    }
  } catch (const std::exception&) {
    // 空 body 也接受：用默认身份
  }
  const std::string token = auth::issue_token(username, role);
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

}  // namespace genealogy::server
