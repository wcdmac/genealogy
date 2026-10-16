#include <genealogy/server/problem.hpp>

#include <string>

namespace genealogy::server {

namespace {

const char* kProblemTypePrefix = "https://genealogy.local/problems/";

// 业务码 → HTTP 状态码。口径 = core 的 ErrorCode（error.hpp 是事实源，
// 那里的注释就写着「与 REST problem+json 的 code 字段对齐」）。
int status_for_code(int code) {
  switch (code) {
    case 1001: return 400;  // 参数校验失败
    case 2001: return 404;  // 资源不存在
    case 2009: return 409;  // 版本冲突
    case 3001: return 401;  // 未认证
    case 4001: return 403;  // 无权限
    case 5001: return 422;  // 历法超出支持范围
    case 6001: return 422;  // 内容无法处理（GEDCOM 解析失败等）
    default:   return 500;  // 9001 内部错误及未知码
  }
}

const char* title_for_code(int code) {
  switch (code) {
    case 1001: return "参数校验失败";
    case 2001: return "资源不存在";
    case 2009: return "版本冲突";
    case 3001: return "未认证";
    case 4001: return "无权限";
    case 5001: return "日期超出支持范围";
    case 6001: return "内容无法处理";
    default:   return "内部错误";
  }
}

// type 的短名（接在前缀后）。与 ErrorCode 一一对应。
const char* slug_for_code(int code) {
  switch (static_cast<ErrorCode>(code)) {
    case ErrorCode::InvalidArgument: return "invalid-argument";
    case ErrorCode::NotFound: return "not-found";
    case ErrorCode::Conflict: return "conflict";
    case ErrorCode::Unauthorized: return "unauthorized";
    case ErrorCode::Forbidden: return "forbidden";
    case ErrorCode::CalendarOutOfRange: return "calendar-out-of-range";
    case ErrorCode::IoError: return "unprocessable";
    case ErrorCode::Internal: return "internal";
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
  resp->setBody(j.dump());
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
  resp->setBody(j.dump());
  finalize(resp, status);
  return resp;
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
  resp->setBody(j.dump());
  finalize(resp, 400);
  return resp;
}

}  // namespace genealogy::server
