#pragma once

// REST 边界上的 JSON 组装与请求体解析。
//
// 分工：core 的 model/*.hpp 已提供 to_json（响应方向），这里补三样：
//   ① openapi 的 XCreate/XUpdate 请求体 → core 模型（create 系列是
//      Person 等模型的子集，不能直接复用 person_from_json，那个要求 id/rev）；
//   ② 分页与 ETag 的解析/生成；
//   ③ 领域 Error → 上面那套 problem+json。
// 响应方向一律用 core 的 to_json，保证 CLI 与 API 吐出同一种表示。

#include <optional>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/model/branch.hpp>
#include <genealogy/model/event.hpp>
#include <genealogy/model/lineage.hpp>
#include <genealogy/model/person.hpp>
#include <genealogy/model/place.hpp>
#include <genealogy/model/union.hpp>
#include <genealogy/server/problem.hpp>

namespace genealogy::server {

// ---------- 通用 ----------

// 必填字符串字段。缺失/空串/非字符串都算错（空串交给用例层报，这里只保类型）。
std::optional<std::string> req_string(const nlohmann::json& j,
                                      const char* key,
                                      std::vector<FieldError>* errors);

std::optional<std::string> opt_string(const nlohmann::json& j, const char* key,
                                      std::vector<FieldError>* errors);

std::optional<int> opt_int(const nlohmann::json& j, const char* key,
                           std::vector<FieldError>* errors);

std::optional<bool> opt_bool(const nlohmann::json& j, const char* key,
                             std::vector<FieldError>* errors);

// 日期字段：GenealogyDate 是复合对象（公历/农历/原文/精度），
// 规范 5.2.1 明确禁止裸日期字符串，故这里只接受对象；缺省返回 nullopt。
std::optional<GenealogyDate> opt_date(const nlohmann::json& j,
                                      const char* key,
                                      std::vector<FieldError>* errors);

// ---------- 请求体 → 模型 ----------

struct PersonDraft {
  std::vector<FieldError> errors;
  Person person;                 // id/rev/时间戳由服务层补
  bool ok() const { return errors.empty(); }
};

PersonDraft parse_person_create(const nlohmann::json& j);
PersonDraft parse_person_update(const nlohmann::json& j);

struct LineageDraft {
  std::vector<FieldError> errors;
  Lineage lineage;
  bool ok() const { return errors.empty(); }
};
LineageDraft parse_lineage_create(const nlohmann::json& j);
LineageDraft parse_lineage_update(const nlohmann::json& j);

struct BranchDraft {
  std::vector<FieldError> errors;
  Branch branch;
  bool ok() const { return errors.empty(); }
};
BranchDraft parse_branch_create(const nlohmann::json& j);

struct PlaceDraft {
  std::vector<FieldError> errors;
  Place place;
  bool ok() const { return errors.empty(); }
};
PlaceDraft parse_place_create(const nlohmann::json& j);

struct UnionDraft {
  std::vector<FieldError> errors;
  Union uni;
  bool ok() const { return errors.empty(); }
};
UnionDraft parse_union_create(const nlohmann::json& j);

struct EventDraft {
  std::vector<FieldError> errors;
  Event event;
  bool ok() const { return errors.empty(); }
};
EventDraft parse_event_create(const nlohmann::json& j);

// ---------- 分页 ----------

struct Page {
  int limit = 50;
  std::string cursor;            // 空 = 第一页
};

Page parse_page(const drogon::HttpRequestPtr& req,
                std::vector<FieldError>* errors);

// 规范 5.2.1 的 page 信封：{"data":[…],"page":{"next_cursor","has_more"}}。
// next_cursor 取本页最后一条的 id（ULID 字典序可排序，仓储层已按此游标翻页）。
nlohmann::json page_envelope(std::vector<nlohmann::json> items,
                             const std::string& next_cursor, bool has_more);

// ---------- ETag / If-Match ----------

// 从 If-Match 头取期望版本；缺失返回 nullopt（调用方决定是否要求必填）。
std::optional<std::int64_t> parse_if_match(const drogon::HttpRequestPtr& req);

// 响应头 ETag: "<rev>"。规范 5.2.1：ETag 带双引号。
void set_etag(const drogon::HttpResponsePtr& resp, std::int64_t rev);

// ---------- 响应便捷 ----------

drogon::HttpResponsePtr json_response(int status, const nlohmann::json& body);
// 201 + ETag（有 rev 的实体用）。无 rev 的实体（event/place）用下面的重载，
// 不设 ETag——给一个恒为 "1" 的假版本号比没有更糟：客户端会拿它做
// If-Match，然后收到莫名其妙的 409。
drogon::HttpResponsePtr json_created(const nlohmann::json& body,
                                     std::int64_t rev);
drogon::HttpResponsePtr json_created(const nlohmann::json& body);
drogon::HttpResponsePtr json_ok(const nlohmann::json& body,
                                std::optional<std::int64_t> rev);

// 把领域 Result 的失败转成 problem+json；成功时返回 nullptr。
drogon::HttpResponsePtr error_to_response(const Error& err,
                                          const std::string& instance);

}  // namespace genealogy::server
