#pragma once

#include <drogon/drogon.h>


// Idempotency-Key 重放（M8.1，设计文档 5.2.1：POST 24h 内重放返回
// 首次结果）。库表持久化：重启后重放仍正确。
//
// 接入形态是 handler 内显式调用（drogon pre-filter 拿不到 handler
// 的响应，advice 层做不了「缓存首次结果」）——POST 写 handler 的
// 标准三段：
//   auto key = req->getHeader("Idempotency-Key");
//   if (auto hit = idempotency_replay(*db, key)) { cb(*hit); return; }
//   … 原有逻辑，成功（2xx）后：
//   idempotency_store(*db, key, status, body);
// key 缺失 = 直接执行（头可选）；仅 2xx 入缓存，失败响应不缓存。
// 命中响应带 Idempotency-Replayed: true 头。

#include <optional>
#include <string>

#include <drogon/HttpResponse.h>
#include <genealogy/repository/database.hpp>

namespace genealogy::server {

// 命中则返回预构造的响应（缓存的 status/body + Replayed 头）。
struct IdempotentReplay {
  drogon::HttpResponsePtr response;
};

// key 是客户端自填的 Idempotency-Key，必须与身份+端点复合后才入缓存：
// 全局单列曾让启用鉴权后不同用户命中彼此的缓存响应、同一 key 跨端点
// 错误复用（2026-11 静态审查 H5）。
std::optional<IdempotentReplay> idempotency_replay(Database& db,
                                                   const drogon::HttpRequestPtr& req,
                                                   const std::string& key);
void idempotency_store(Database& db, const drogon::HttpRequestPtr& req,
                       const std::string& key, int status,
                       const std::string& body);

}  // namespace genealogy::server
