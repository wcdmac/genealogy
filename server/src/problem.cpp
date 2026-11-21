#include <genealogy/server/problem.hpp>

#include <cstdio>

#include <string>

namespace genealogy::server {

namespace {

const char* kProblemTypePrefix = "https://genealogy.local/problems/";

// 业务码 → HTTP 状态码。口径 = core 的 ErrorCode（error.hpp 是事实源，
// 那里的注释就写着「与 REST problem+json 的 code 字段对齐」）。
int status_for_code(int code) {
  switch (code) {
    case 1001: return 400;  // 参数校验失败
    case 1002: return 415;  // 不支持的媒体类型（第三轮新-8 补映射）
    case 2001: return 404;  // 资源不存在
    case 2009: return 409;  // 版本冲突
    case 3001: return 401;  // 未认证
    case 4001: return 403;  // 无权限
    case 5001: return 422;  // 历法超出支持范围
    case 6001: return 422;  // 内容无法处理（GEDCOM 解析失败等）
    case 42901: return 429;  // 限流
    case 9002: return 503;  // SSE 订阅数达上限
    default:   return 500;  // 9001 内部错误及未知码
  }
}

const char* title_for_code(int code) {
  switch (code) {
    case 1001: return "参数校验失败";
    case 1002: return "不支持的媒体类型";
    case 2001: return "资源不存在";
    case 2009: return "版本冲突";
    case 3001: return "未认证";
    case 4001: return "无权限";
    case 5001: return "日期超出支持范围";
    case 6001: return "内容无法处理";
    case 42901: return "请求过于频繁";
    case 9002: return "订阅数已达上限";
    default:   return "内部错误";
  }
}

// type 的短名（接在前缀后）。与 ErrorCode 一一对应。
const char* slug_for_code(int code) {
  switch (static_cast<ErrorCode>(code)) {
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::UnsupportedMediaType: return "unsupported-media-type";
    case ErrorCode::NotFound: return "not-found";
    case ErrorCode::Conflict: return "conflict";
    case ErrorCode::Unauthorized: return "unauthorized";
    case ErrorCode::Forbidden: return "forbidden";
    case ErrorCode::CalendarOutOfRange: return "calendar-out-of-range";
    case ErrorCode::IoError: return "unprocessable";
    case ErrorCode::Internal: return "internal";
    case ErrorCode::TooManyRequests: return "too-many-requests";
    case ErrorCode::ServiceUnavailable: return "service-unavailable";
    case ErrorCode::Ok: break;
  }
  return "internal";
}

// 收尾：状态码 + 媒体类型。所有 problem 响应都走这里，保证
// Content-Type 只有一个（用 addHeader 会留下两个，HTTP 不允许重复该头，
// 而不同客户端取第一个还是最后一个，行为就不确定了）。
// Drogon 的内容类型枚举里没有 problem+json，只能覆写字符串。
void finalize(const drogon::HttpResponsePtr& resp, int status) {
  resp->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
  resp->setContentTypeString("application/problem+json; charset=utf-8");
}

}  // namespace

int http_status_for(ErrorCode code) {
  return status_for_code(static_cast<int>(code));
}

const char* problem_type_for(ErrorCode code) {
  return slug_for_code(static_cast<int>(code));
}

drogon::HttpResponsePtr problem_response(const Error& err,
                                        const std::string& instance) {
  // Internal 的 detail 不回显原文——sqlite errmsg 会带出表/列/约束名，
  // 客户端可据此探测 schema（2026-11 静态审查 M10）；原文走服务端日志。
  if (err.code() == ErrorCode::Internal) {
    std::fputs((std::string("[internal] ") + instance + ": " +
                err.message() + std::string(1, static_cast<char>(10)))
                   .c_str(),
               stderr);
    const int icode = static_cast<int>(ErrorCode::Internal);
    return problem_response(status_for_code(icode), icode,
                            title_for_code(icode),
                            "内部错误（详情见服务端日志）", instance,
                            problem_type_for(ErrorCode::Internal));
  }
  const int code = static_cast<int>(err.code());
  const int status = status_for_code(code);
  nlohmann::json j;
  j["type"] = std::string(kProblemTypePrefix) + slug_for_code(code);
  j["title"] = title_for_code(code);
  j["status"] = status;
  j["detail"] = err.message();
  j["code"] = code;       // 业务码：status 说 HTTP 层，code 说领域层
  j["instance"] = instance;
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
  finalize(resp, status);
  return resp;
}

drogon::HttpResponsePtr problem_response(int status, int code,
                                         const std::string& title,
                                         const std::string& detail,
                                         const std::string& instance,
                                         const std::string& type) {
  nlohmann::json j;
  j["type"] = type.empty() ? std::string(kProblemTypePrefix) + slug_for_code(code)
                           : type;
  j["title"] = title;
  j["status"] = status;
  if (!detail.empty()) j["detail"] = detail;
  j["code"] = code;
  j["instance"] = instance;
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
  finalize(resp, status);
  return resp;
}

drogon::HttpResponsePtr bad_json_body(const std::string& parser_what,
                                      const std::string& instance) {
  // 解析器原文只进服务端日志（同 M10 的 Internal 脱敏纪律），响应里的
  // detail 是固定文案（复核 2.3：此前 12 处把 e.what() 直拼进 detail）。
  std::fputs((std::string("[bad-json] ") + instance + ": " + parser_what +
              std::string(1, static_cast<char>(10)))
                 .c_str(),
             stderr);
  return problem_response(400, 1001, "参数校验失败",
                          "请求体不是合法 JSON", instance);
}

drogon::HttpResponsePtr validation_problem(
    const std::string& instance, const std::vector<FieldError>& errors,
    const std::string& detail) {
  nlohmann::json j;
  j["type"] = std::string(kProblemTypePrefix) + "invalid-argument";
  j["title"] = title_for_code(1001);
  j["status"] = 400;
  if (!detail.empty()) j["detail"] = detail;
  j["code"] = 1001;
  j["instance"] = instance;
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& e : errors) {
    arr.push_back({{"field", e.field}, {"message", e.message}});
  }
  j["errors"] = std::move(arr);
  auto resp = drogon::HttpResponse::newHttpResponse();
  resp->setBody(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
  finalize(resp, 400);
  return resp;
}

}  // namespace genealogy::server
