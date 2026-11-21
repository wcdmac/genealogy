#pragma once

// RFC 9457 problem+json 错误响应，字段与 openapi 的 Problem schema 对齐
// （设计文档 5.2.1）：{type, title, status, detail, code, instance, errors[]}。
//
// 领域层的 Error.code() 已经是规范里的那套业务码（1001/2001/2009/5001/6001…），
// 这里只负责把「业务码 → HTTP 状态码」这一层映射做对，并保证所有失败路径
// 吐出同一种结构——客户端生成器靠这个吃饭，散着写必然漂移。

#include <string>
#include <vector>

#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>

namespace genealogy::server {

// 业务错误码 → HTTP 状态码。口径 = core 的 ErrorCode（error.hpp 是事实源，
// 那里的注释就写着「与 REST problem+json 的 code 字段对齐」）：
//   1001 参数校验失败 → 400
//   2001 资源不存在   → 404
//   2009 版本冲突     → 409
//   3001 未认证       → 401
//   4001 无权限       → 403
//   5001 历法越界     → 422
//   6001 内容无法处理 → 422（GEDCOM 解析失败等：请求合法但内容处理不了）
//   9001 内部错误     → 500
int http_status_for(ErrorCode code);

// type 字段的 URI 前缀。规范要求是 URI，实际客户端只做展示，
// 用稳定的、可读的伪 URI 而不是不可点的 urn。
const char* problem_type_for(ErrorCode code);

// 由领域 Error 构造 problem+json 响应。detail 用错误消息，
// instance 由调用方填（通常是请求路径）。
drogon::HttpResponsePtr problem_response(const Error& err,
                                         const std::string& instance);

// 便捷：直接给状态码与文案（用于框架层错误，如 JSON 解析失败、404 路由）。
// type 留空时按 code 推导——绝大多数调用点不需要操心这个 URI。
drogon::HttpResponsePtr problem_response(int status, int code,
                                         const std::string& title,
                                         const std::string& detail,
                                         const std::string& instance,
                                         const std::string& type = "");

// 请求体 JSON 解析失败的统一出口（复核 2.3）：detail 用固定文案——
// e.what() 会带出解析器内部细节（字节偏移、解析栈、库版本指纹等），
// 与 M10「内部细节走服务端日志」同一纪律；原文只写 stderr，不进响应。
// 全部 handler 的 catch 块都走这里，不许再散写 problem_response(400,…)。
drogon::HttpResponsePtr bad_json_body(const std::string& parser_what,
                                      const std::string& instance);

// 字段级校验错误（code 1001 时附带 errors[]）。
struct FieldError {
  std::string field;
  std::string message;
};

drogon::HttpResponsePtr validation_problem(
    const std::string& instance, const std::vector<FieldError>& errors,
    const std::string& detail);

}  // namespace genealogy::server
