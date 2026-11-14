#pragma once

// JSON 请求体守卫：Content-Type 为 application/json 的请求，body 必须
// 是合法 UTF-8，否则 400 short-circuit。
//
// 为什么要在门口拦：无效 UTF-8 的 JSON 体一旦进入 json::parse 会触发
// 进程级终止（实测：drogon 的 handler try/catch 与 std::set_terminate、
// SetUnhandledExceptionFilter 都没机会跑，症状是连接被丢弃、进程消失，
// 见 2026-11-12 GBK 请求体排查记录）。成因在第三方解析栈深处，
// 与其追到黑盒里，不如让非法输入根本进不来——RFC 8259 本来就规定
// JSON 交换文本必须是 UTF-8，拒绝是合规行为而非兼容性牺牲。
//
// 非 JSON 体（GEDCOM 导入等）不检查：老谱软件导出的 ANSI/GBK GEDCOM
// 是合法输入，由 io::parse_gedcom 自己做字符集处理。

#include <drogon/HttpAppFramework.h>

namespace genealogy::server {

// 注册 pre-routing 守卫。main 与测试二进制都要装——守卫是横切行为。
void install_body_guard(drogon::HttpAppFramework& app);

}  // namespace genealogy::server
