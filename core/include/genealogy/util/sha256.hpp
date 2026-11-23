#pragma once

// SHA-256 摘要（FIPS 180-4 算法本身为公有领域，此处自实现，零新增依赖）。
// 用途：附件去重与完整性校验——不是安全/认证场景。

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace genealogy {

// 返回 64 字符小写十六进制
std::string sha256_hex(std::string_view data);
std::string sha256_file(const std::string& path);

}  // namespace genealogy
