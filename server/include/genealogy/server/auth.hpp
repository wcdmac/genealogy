#pragma once

// JWT（HS256）签发与校验——REST 的 /auth/token 与 /me 用。
//
// 为什么自带实现而不是引 jwt-cpp：HS256 只需要 SHA-256 + HMAC +
// base64url 三样，加起来两百行且完全可测；为一个 header-only 库
// 再开一条依赖链不值得（项目约束：依赖只收宽松许可且尽量少）。
//
// 本地模式默认免鉴权（绑 127.0.0.1），此模块仅在 --require-auth 时生效。
// 密钥从环境变量 GENEALOGY_JWT_SECRET 读；未设时用编译期内置的默认值
// 并打警告——默认值只够本地开发，公网部署必须显式设密钥。

#include <cstdint>
#include <optional>
#include <string>

namespace genealogy::server {

namespace auth {

// 三角色（设计文档 5.2.4）：steward 族尊（含删除与规则配置）/
// editor 编修 / viewer 只读。
enum class Role { Steward, Editor, Viewer };

const char* to_string(Role r);
std::optional<Role> role_from_string(const std::string& s);

struct Claims {
  std::string username;
  Role role = Role::Viewer;
  std::int64_t expires_at = 0;   // Unix 秒
  // 在世者脱敏豁免（M8.3）：true = 看到完整生卒/住址/传记。豁免依据
  // 是令牌 claim 而非查询参数——查询参数会被日志与转发记录，token
  // 才是豁免的正确载体（云版规划既定方向）。本地单机身份恒为 true
  //（开箱即用，对应设计「GUI 显式开关仅本地库有效」）；require-auth
  // 签发的令牌恒为 false，云端永不签发 true。
  bool raw_privacy = false;
};

// 签发。ttl_seconds 默认 24h；raw_privacy 仅本地便捷通道使用，
// require-auth 模式固定 false（云端永不签发豁免令牌）。
std::string issue_token(const std::string& username, Role role,
                        std::int64_t ttl_seconds = 24 * 3600,
                        bool raw_privacy = false);

// 校验。失败原因放在 error 里（过期 / 签名不符 / 结构损坏）。
bool verify_token(const std::string& token, Claims* out, std::string* error);

// 本地免鉴权模式下使用的固定身份。
Claims local_identity();

}  // namespace auth

}  // namespace genealogy::server
