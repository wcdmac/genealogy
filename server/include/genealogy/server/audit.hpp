#pragma once

// audit_log 接线（M8.5，设计文档 4.9：全部写操作的 JSON diff，按实体
// 回溯）。
//
// 写入在 server 层做而不是 service 层——actor 身份（token username）
// 只在 REST 可得，service 签名加 actor 是大改。diff 由 handler 顺手
// 提供：POST=新建实体、DELETE=被删实体（删前取）、PATCH=before/after
// 的字段级对比。
//
// 审计写失败不阻断主写：主写已在自身事务里成功，审计是观测面，
// 失败仅记 stderr——但绝不静默（stderr 是运维可见面）。

#include <drogon/HttpRequest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <genealogy/error.hpp>
#include <genealogy/repository/database.hpp>

namespace genealogy::server {

// 仅记录发生变化的顶层字段：{field: {"before":…, "after":…}}。
// before/after 任一缺失（新建/删除）则另一侧为 null。
nlohmann::json json_diff(const nlohmann::json& before,
                         const nlohmann::json& after);

// 写一条审计。actor 取当前请求身份（identity_of；本地模式 "local"）。
void audit_write(Database& db, const drogon::HttpRequestPtr& req,
                 const std::string& entity_type, const std::string& entity_id,
                 const std::string& action, const nlohmann::json& diff);

struct AuditRow {
  std::string id;
  std::string entity_type;
  std::string entity_id;
  std::string action;
  std::string actor;
  std::string at;
  nlohmann::json diff;
};

struct AuditQuery {
  std::string entity_type;  // 空 = 不过滤
  std::string entity_id;    // 空 = 不过滤
  int limit = 50;
  std::string cursor;       // id 倒序 keyset（ULID 时间有序，新变更在前）
};

// /audit 查询：按实体过滤，id 倒序 keyset 分页。
Result<std::vector<AuditRow>> audit_list(Database& db, const AuditQuery& q);

}  // namespace genealogy::server
