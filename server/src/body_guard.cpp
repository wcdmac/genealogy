#include <genealogy/server/body_guard.hpp>

#include <string>
#include <string_view>

#include <genealogy/server/problem.hpp>

namespace genealogy::server {

namespace {

// 严格 UTF-8 校验（RFC 3629）：单遍 DFA，拒过度长编码/代理区/越界。
// 只在请求门口跑一次，O(n) 且无分配。
bool is_valid_utf8(const char* data, std::size_t len) {
  std::size_t i = 0;
  while (i < len) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    std::size_t need = 0;
    unsigned char lo = 0x80, hi = 0xBF;  // 后续字节统一 0x80-0xBF
    if (c >= 0xC2 && c <= 0xDF) {
      need = 1;
    } else if (c == 0xE0) {
      need = 2;
      lo = 0xA0;  // 拒过度长编码（0xE0 0x80-0x9F）
    } else if (c >= 0xE1 && c <= 0xEC) {
      need = 2;
    } else if (c == 0xED) {
      need = 2;
      hi = 0x9F;  // 拒代理区（0xED 0xA0-0xBF）
    } else if (c >= 0xEE && c <= 0xEF) {
      need = 2;
    } else if (c == 0xF0) {
      need = 3;
      lo = 0x90;  // 拒过度长编码
    } else if (c >= 0xF1 && c <= 0xF3) {
      need = 3;
    } else if (c == 0xF4) {
      need = 3;
      hi = 0x8F;  // 拒 > U+10FFFF
    } else {
      return false;  // 0x80-0xC1（续字节/过度长首字节）、0xF5-0xFF
    }
    // 连续字节位于 i+1..i+need，必须完整落在缓冲区内
    if (i + need >= len) return false;
    for (std::size_t k = 1; k <= need; ++k) {
      const unsigned char cc = static_cast<unsigned char>(data[i + k]);
      const unsigned char lower = (k == 1) ? lo : 0x80;
      const unsigned char upper = (k == 1) ? hi : 0xBF;
      if (cc < lower || cc > upper) return false;
    }
    i += need + 1;
  }
  return true;
}

}  // namespace

void install_body_guard(drogon::HttpAppFramework& app) {
  app.registerPreRoutingAdvice(
      [](const drogon::HttpRequestPtr& req,
         drogon::AdviceCallback&& acb,
         drogon::AdviceChainCallback&& accb) {
        // RFC 9110：media type 不区分大小写——头值先归一小写再匹配，
        // 否则 Application/JSON 这类怪头会绕过守卫（GBK body 直达
        // json::parse，正是本守卫要堵的进程级终止路径）。
        std::string ctype = req->getHeader("content-type");
        for (char& c : ctype) {
          if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        const auto body = req->body();
        if (body.empty()) {
          accb();
          return;
        }
        if (ctype.find("application/json") == std::string::npos) {
          // multipart 是 GEDCOM 导入的合法载体（文件自带字符集检测，
          // 不能按 UTF-8 强校验）；且仅此一个端点消费 multipart——
          // 其余端点即便带 multipart 头也一律 415。此前对任何路径的
          // multipart 都放行，构造 `multipart/form-data` 头 + 非法
          // UTF-8 body 可绕过守卫直达各 JSON 端点的 json::parse，
          // 进程级终止路径仍可达（复核 2.5 残余）。
          if (ctype.find("multipart/") != std::string::npos) {
            // 只认以 /imports/gedcom 结尾的路径（base 前缀 /api/v1 不参与
            // 匹配，前缀调整不会破坏该判定）。
            static constexpr std::string_view kMultipartPath =
                "/imports/gedcom";
            const std::string path = req->path();
            if (path.size() >= kMultipartPath.size() &&
                path.compare(path.size() - kMultipartPath.size(),
                             kMultipartPath.size(), kMultipartPath) == 0) {
              accb();
              return;
            }
            acb(problem_response(415, 1002, "不支持的媒体类型",
                                 "multipart 请求体仅 GEDCOM 导入端点消费",
                                 path));
            return;
          }
          acb(problem_response(415, 1002, "不支持的媒体类型",
                               "API 只接受 application/json 或 "
                               "multipart/form-data 请求体",
                               req->path()));
          return;
        }
        if (!is_valid_utf8(body.data(), body.size())) {
          auto resp = problem_response(
              400, 1001, "参数校验失败",
              "请求体不是合法 UTF-8（JSON 交换文本按 RFC 8259 必须 UTF-8）",
              req->path());
          acb(resp);
          return;
        }
        accb();
      });
}

}  // namespace genealogy::server
