// genealogy-server：REST 服务入口（M2）。
//
// 设计文档 3.3 / 5.2：
//   · base path /api/v1，版本只由路径承载；
//   · 本地模式默认绑 127.0.0.1 且免鉴权，可显式开启；
//   · 数据库连接在启动时建好，经请求上下文带给各路由——
//     路由函数不碰连接生命周期，也不自己开连接；
//   · /openapi.json 原样托管 shared/openapi/openapi.yaml
//     （客户端代码生成的唯一事实源）。

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <drogon/drogon.h>
#include <drogon/HttpAppFramework.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/server/cors.hpp>
#include <genealogy/server/event_bus.hpp>
#include <genealogy/server/routes_entities.hpp>
#include <genealogy/server/routes_misc.hpp>
#include <genealogy/server/routes_auth.hpp>
#include <genealogy/server/routes_stream.hpp>
#include <genealogy/server/runtime.hpp>
#include <genealogy/server/routes_persons.hpp>

namespace {

using namespace genealogy;
using namespace genealogy::server;
using genealogy::server::Callback;

constexpr const char* kVersion = "1.0.0";

struct Options {
  std::string host = "127.0.0.1";
  std::uint16_t port = 8080;
  std::string db_path = "./genealogy.db";
  std::size_t threads = 4;
  bool require_auth = false;
};

Options parse_args(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "参数 " << what << " 缺值\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--host") {
      o.host = next("--host");
    } else if (a == "--port") {
      try {
        o.port = static_cast<std::uint16_t>(std::stoul(next("--port")));
      } catch (const std::exception&) {
        std::cerr << "--port 应为 1..65535\n";
        std::exit(2);
      }
    } else if (a == "--db") {
      o.db_path = next("--db");
    } else if (a == "--threads") {
      try {
        o.threads = static_cast<std::size_t>(std::stoul(next("--threads")));
      } catch (const std::exception&) {
        std::cerr << "--threads 应为正整数\n";
        std::exit(2);
      }
    } else if (a == "--require-auth") {
      o.require_auth = true;
    } else if (a == "--help" || a == "-h") {
      std::cout << "genealogy-server " << kVersion << "\n"
                << "用法: genealogy-server [--host 127.0.0.1] [--port 8080]\n"
                << "                          [--db PATH] [--threads N] [--require-auth]\n";
      std::exit(0);
    } else {
      std::cerr << "未知参数: " << a << "（--help 看用法）\n";
      std::exit(2);
    }
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parse_args(argc, argv);

  // 库打不开就别起服务：起一个连库都连不上的服务，比明确失败更难排查
  auto db = Database::open(opt.db_path);
  if (!db) {
    std::cerr << "打不开数据库 " << opt.db_path << ": "
              << db.error().to_string() << "\n";
    return 1;
  }
  // 兼容 M0 遗留库：确保占位宗族存在（M1 起有 lineage 管理命令）
  ensure_default_lineage(*db);

  // 连接与鉴权开关交给 runtime 单例，路由经请求上下文取用
  bind_runtime(*db, opt.require_auth);

  // 本地模式免鉴权：角色一律给 steward，让 CLI/桌面端开箱即用。
  // 这不是安全漏洞——默认只绑 127.0.0.1，只有本机能连；
  // 要对公网开放必须 --require-auth 并另配 JWT 校验。
  // HttpAppFramework 是抽象类且 NonCopyable，不能直接实例化——
  // 走单例访问器（drogon::app() 是同一个东西的简写）。
  auto& app = drogon::HttpAppFramework::instance();

  // ---- 路径注册：base path /api/v1 ----
  const std::string base = "/api/v1";

  // 鉴权
  app.registerHandler(base + "/auth/token", &server::auth_token,
                      {drogon::Post});
  app.registerHandler(base + "/me", &server::me, {drogon::Get});

  // 运维
  app.registerHandler(base + "/health", &health, {drogon::Get});
  app.registerHandler("/openapi.yaml", &openapi_doc, {drogon::Get});

  // 人物
  app.registerHandler(base + "/persons", &persons_list, {drogon::Get});
  app.registerHandler(base + "/persons", &persons_create, {drogon::Post});
  app.registerHandler(base + "/persons/{id}", &person_get, {drogon::Get});
  app.registerHandler(base + "/persons/{id}", &person_patch, {drogon::Patch});
  app.registerHandler(base + "/persons/{id}", &person_delete, {drogon::Delete});

  // 世系算法
  app.registerHandler(base + "/persons/{id}/ancestors", &person_ancestors,
                      {drogon::Get});
  app.registerHandler(base + "/persons/{id}/descendants", &person_descendants,
                      {drogon::Get});
  app.registerHandler(base + "/persons/{id}/tree", &person_tree, {drogon::Get});
  app.registerHandler(base + "/persons/{id}/mourning-degree",
                      &person_mourning, {drogon::Get});
  app.registerHandler(base + "/persons/{id}/appellation",
                      &person_appellation, {drogon::Get});

  // 基础资源
  app.registerHandler(base + "/lineages", &lineages_list, {drogon::Get});
  app.registerHandler(base + "/lineages", &lineages_create, {drogon::Post});
  app.registerHandler(base + "/lineages/{id}", &lineage_get, {drogon::Get});
  app.registerHandler(base + "/lineages/{id}", &lineage_patch, {drogon::Patch});
  app.registerHandler(base + "/lineages/{id}", &lineage_delete,
                      {drogon::Delete});

  app.registerHandler(base + "/branches", &branches_list, {drogon::Get});
  app.registerHandler(base + "/branches", &branches_create, {drogon::Post});
  app.registerHandler(base + "/branches/{id}", &branch_get, {drogon::Get});
  app.registerHandler(base + "/branches/{id}", &branch_patch, {drogon::Patch});
  app.registerHandler(base + "/branches/{id}", &branch_delete,
                      {drogon::Delete});
  app.registerHandler(base + "/branches/{id}/tree", &branch_tree,
                      {drogon::Get});

  app.registerHandler(base + "/unions", &unions_list, {drogon::Get});
  app.registerHandler(base + "/unions", &unions_create, {drogon::Post});
  app.registerHandler(base + "/unions/{id}", &union_get, {drogon::Get});
  app.registerHandler(base + "/unions/{id}", &union_patch, {drogon::Patch});
  app.registerHandler(base + "/unions/{id}", &union_delete, {drogon::Delete});

  app.registerHandler(base + "/events", &events_list, {drogon::Get});
  app.registerHandler(base + "/events", &events_create, {drogon::Post});
  app.registerHandler(base + "/events/{id}", &event_get, {drogon::Get});
  app.registerHandler(base + "/events/{id}", &event_delete, {drogon::Delete});

  app.registerHandler(base + "/places", &places_list, {drogon::Get});
  app.registerHandler(base + "/places", &places_create, {drogon::Post});
  app.registerHandler(base + "/places/{id}", &place_get, {drogon::Get});
  app.registerHandler(base + "/places/{id}", &place_patch, {drogon::Patch});
  app.registerHandler(base + "/places/{id}", &place_delete, {drogon::Delete});

  // 迁徙（M5-4）
  app.registerHandler(base + "/migrations", &migrations_list, {drogon::Get});
  app.registerHandler(base + "/migrations", &migrations_create, {drogon::Post});

  // 检索 / 统计 / 校验 / 导入导出
  app.registerHandler(base + "/search", &search, {drogon::Get});
  app.registerHandler(base + "/stats", &stats, {drogon::Get});
  app.registerHandler(base + "/validate", &validate_run, {drogon::Post});
  app.registerHandler(base + "/imports/gedcom", &import_gedcom,
                      {drogon::Post});
  app.registerHandler(base + "/exports/gedcom", &export_gedcom, {drogon::Get});
  app.registerHandler(base + "/exports/tree", &export_tree, {drogon::Get});

  // SSE 变更流（多端协同；本地单机模式同样无害）
  app.registerHandler(base + "/events/stream", &stream_events, {drogon::Get});
  // 事件总线：写操作广播 + 15s ping 定时器
  EventBus::instance().install(app);

  // 404 也走 problem+json：客户端生成器对错误体的解析只有一条路径，
  // 框架默认的 HTML 404 会让它炸在第 3 方库里而不是业务代码里。
  // 404 也走 problem+json：客户端生成器对错误体的解析只有一条路径，
  // 框架默认的 HTML 404 会让它炸在第 3 方库里而不是业务代码里。
  // 用 nlohmann 组包而不是拼字符串——手拼 JSON 漏个引号，
  // 故障就发生在最不容易看的路径上（本次即因此返工）。
  app.setDefaultHandler(
      [](const drogon::HttpRequestPtr& req,
         std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
        nlohmann::json body;
        body["type"] = "https://genealogy.local/problems/not-found";
        body["title"] = "资源不存在";
        body["status"] = 404;
        body["detail"] = req->path() + " 没有对应处理函数";
        body["instance"] = req->path();
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(body.dump());
        resp->setStatusCode(drogon::k404NotFound);
        resp->setContentTypeString("application/problem+json; charset=utf-8");
        cb(resp);
      });

  app.addListener(opt.host, opt.port);
  app.setThreadNum(std::max<std::size_t>(1, opt.threads));

  // CORS：Web 客户端外接必需（设计文档 5.2）。默认放开（只绑回环，
  // 无跨站风险）；GENEALOGY_CORS_ORIGINS 收紧白名单。
  const char* cors_env = std::getenv("GENEALOGY_CORS_ORIGINS");
  install_cors(app, cors_env != nullptr ? cors_env : "");
  // 请求体上限：GEDCOM 导入走 multipart，万条人物约几十 MB，
  // 用 Drogon 默认上限会直接 413。上限放到 512 MB——
  // 再大的导入应走 CLI 的 io import（流式），HTTP 边界不做流式。
  app.setClientMaxBodySize(512ULL * 1024 * 1024);
  // 不开静态文件服务：/openapi.yaml 由 openapi_doc 处理器按
  // 「可执行文件目录 → cwd → 仓库相对路径」的顺序从磁盘读，
  // 比把 document root 绑死在 ./ 更稳（换工作目录不会 500）。

  std::cout << "genealogy-server " << kVersion << " 监听 http://" << opt.host
            << ":" << opt.port << base << "（库: " << opt.db_path
            << "，鉴权: " << (opt.require_auth ? "开启" : "本地免鉴权") << "）\n";
  app.run();
  return 0;
}
