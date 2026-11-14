// M2 REST 集成测试：起真实 Drogon 服务打真实 HTTP。
//
// 为什么不 mock：这一层要验的恰恰是「路由 + DTO + 用例层 + 数据库」
// 串起来的行为——状态码、problem+json 结构、ETag/If-Match、分页游标。
// mock 掉任何一层，这些就都验不到了。
//
// 关于夹具：Drogon 的 HttpAppFramework 是**进程级单例**，设计为起一次。
// 每个用例各起一套（registerHandler + run + quit）会撞上单例的重复注册
// 与重启语义——第一个用例之后路由表越堆越乱，且 quit 后能否再 run
// 并没有保证。故这里只起一次服务，每个用例换一份临时库：
// 用例之间用 bind_runtime 切库，顺序执行的 Catch2 下没有竞争。

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <drogon/drogon.h>
#include <nlohmann/json.hpp>

#include <genealogy/error.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/ulid.hpp>
#include <genealogy/server/auth.hpp>
#include <genealogy/server/body_guard.hpp>
#include <genealogy/server/cors.hpp>
#include <genealogy/server/routes_entities.hpp>
#include <genealogy/server/routes_misc.hpp>
#include <genealogy/server/routes_sources.hpp>
#include <genealogy/server/routes_persons.hpp>
#include <genealogy/server/routes_auth.hpp>
#include <genealogy/server/runtime.hpp>

using namespace genealogy;
using namespace genealogy::server;   // bind_runtime / Callback 等

namespace {

// 整个测试二进制只起一次服务。端口由内核分配（传 0）。
//
// 端口必须放在静态变量里再由每个夹具拷走：std::call_once 只会在
// 第一个夹具构造时执行，若把端口写进调用方的成员，第二个起夹具的
// port 永远是 0 —— 而 newHttpClient("127.0.0.1", 0) 只会得到
// ReqResult::BadServerAddress（3），症状是「第一个用例全过、
// 其余全挂」，极难一眼看穿。
// 服务线程与端口放在命名空间作用域而非函数内静态：停服函数
// （stop_server）也要够到它们。测试二进制里只有一个实例，无竞争。
std::atomic<bool> g_started{false};
std::uint16_t g_bound_port = 0;
std::thread g_server_thread;
std::once_flag g_once;

void start_server_once(std::uint16_t* port_out) {
  std::call_once(g_once, [] {
    auto& app = drogon::HttpAppFramework::instance();
    app.setThreadNum(1);
    app.setClientMaxBodySize(64ULL * 1024 * 1024);
    // CORS 与鉴权守卫与 main.cpp 同一套安装函数：鉴权是横切行为，
    // 测试二进制不装守卫的话，「--require-auth 全端点生效」就只在
    // 生产装了、测试永远是免鉴权路径，401 行为完全没被覆盖。
    install_cors(app, "");            // 默认策略：只放行本机来源
    install_body_guard(app);          // JSON 体 UTF-8 预校验
    install_auth_guard(app);
    // 库句柄与鉴权开关由 bind_runtime 单例提供（db_from/
    // require_auth_of 都会退到进程级），与 main.cpp 同一条路径。
    const std::string base = "/api/v1";
    app.registerHandler(base + "/health", &server::health, {drogon::Get});
    app.registerHandler(base + "/auth/token", &server::auth_token,
                        {drogon::Post});
    app.registerHandler(base + "/me", &server::me, {drogon::Get});
    app.registerHandler(base + "/persons", &server::persons_list,
                        {drogon::Get});
    app.registerHandler(base + "/persons", &server::persons_create,
                        {drogon::Post});
    app.registerHandler(base + "/persons/{id}", &server::person_get,
                        {drogon::Get});
    app.registerHandler(base + "/persons/{id}", &server::person_patch,
                        {drogon::Patch});
    app.registerHandler(base + "/persons/{id}", &server::person_delete,
                        {drogon::Delete});
    app.registerHandler(base + "/persons/{id}/ancestors",
                        &server::person_ancestors, {drogon::Get});
    app.registerHandler(base + "/persons/{id}/descendants",
                        &server::person_descendants, {drogon::Get});
    app.registerHandler(base + "/persons/{id}/mourning-degree",
                        &server::person_mourning, {drogon::Get});
    app.registerHandler(base + "/persons/{id}/appellation",
                        &server::person_appellation, {drogon::Get});
    app.registerHandler(base + "/lineages", &server::lineages_list,
                        {drogon::Get});
    app.registerHandler(base + "/lineages", &server::lineages_create,
                        {drogon::Post});
    app.registerHandler(base + "/lineages/{id}", &server::lineage_get,
                        {drogon::Get});
    app.registerHandler(base + "/lineages/{id}", &server::lineage_patch,
                        {drogon::Patch});
    app.registerHandler(base + "/lineages/{id}", &server::lineage_delete,
                        {drogon::Delete});
    // 支派（按支派查迁徙的用例需要）
    app.registerHandler(base + "/branches", &server::branches_list,
                        {drogon::Get});
    app.registerHandler(base + "/branches", &server::branches_create,
                        {drogon::Post});
    app.registerHandler(base + "/branches/{id}", &server::branch_get,
                        {drogon::Get});
    app.registerHandler(base + "/places", &server::places_list,
                        {drogon::Get});
    app.registerHandler(base + "/places", &server::places_create,
                        {drogon::Post});
    app.registerHandler(base + "/places/{id}", &server::place_get,
                        {drogon::Get});
    app.registerHandler(base + "/places/{id}", &server::place_patch,
                        {drogon::Patch});
    app.registerHandler(base + "/places/{id}", &server::place_delete,
                        {drogon::Delete});
    // 证据链（M7.2）：来源与证据引用
    app.registerHandler(base + "/sources", &server::sources_list,
                        {drogon::Get});
    app.registerHandler(base + "/sources", &server::sources_create,
                        {drogon::Post});
    app.registerHandler(base + "/sources/{id}", &server::source_get,
                        {drogon::Get});
    app.registerHandler(base + "/sources/{id}", &server::source_patch,
                        {drogon::Patch});
    app.registerHandler(base + "/sources/{id}", &server::source_delete,
                        {drogon::Delete});
    app.registerHandler(base + "/citations", &server::citations_list,
                        {drogon::Get});
    app.registerHandler(base + "/citations", &server::citations_create,
                        {drogon::Post});
    app.registerHandler(base + "/citations/{id}", &server::citation_get,
                        {drogon::Get});
    app.registerHandler(base + "/citations/{id}", &server::citation_patch,
                        {drogon::Patch});
    app.registerHandler(base + "/citations/{id}", &server::citation_delete,
                        {drogon::Delete});
    // 迁徙（M5-4）：expand 测试要挂迁徙子资源，注册与 main.cpp 对齐
    app.registerHandler(base + "/migrations", &server::migrations_list,
                        {drogon::Get});
    app.registerHandler(base + "/migrations", &server::migrations_create,
                        {drogon::Post});
    app.registerHandler(base + "/unions", &server::unions_list,
                        {drogon::Get});
    app.registerHandler(base + "/unions", &server::unions_create,
                        {drogon::Post});
    app.registerHandler(base + "/events", &server::events_list,
                        {drogon::Get});
    app.registerHandler(base + "/events", &server::events_create,
                        {drogon::Post});
    app.registerHandler(base + "/search", &server::search, {drogon::Get});
    app.registerHandler(base + "/stats", &server::stats, {drogon::Get});
    app.registerHandler(base + "/validate", &server::validate_run,
                        {drogon::Post});
    // 404 也走 problem+json（与 main.cpp 同口径）
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

    // 线程里自取单例，不捕获 app：app 是外层 lambda 的引用型局部量，
    // 隐式捕获会撞 C3493；显式捕获引用又容易看糊。单例本来就地可取。
    g_server_thread = std::thread([] {
      auto& app = drogon::HttpAppFramework::instance();
      app.addListener("127.0.0.1", 0);
      app.run();
    });
    // 等监听就绪：轮询 getListeners，最多 10 秒。
    // getListeners() 返回 vector<InetAddress>（值，不是指针），故用 . 而非 ->
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      const auto listeners = app.getListeners();
      if (!listeners.empty() && listeners.front().toPort() != 0) {
        g_bound_port = listeners.front().toPort();
        g_started = true;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
  *port_out = g_bound_port;
  REQUIRE(g_started.load());
  REQUIRE(*port_out != 0);
}

// 停服：让 app.run() 返回并 join 服务线程。
// 为什么必须做：不 quit 的话，进程退出时静态析构会撞上还在跑的事件
// 循环——症状是「全部用例通过、退出码 0xC0000409（fail-fast）」，CI
// 直接判红，而 Catch2 的摘要还是全绿，极具迷惑性。Drogon 自家测试
// 也是这个收尾法（queueInLoop([]{ app().quit(); })）。
void stop_server() {
  if (!g_started.load()) return;
  auto& app = drogon::HttpAppFramework::instance();
  app.getLoop()->queueInLoop([] { drogon::app().quit(); });
  if (g_server_thread.joinable()) g_server_thread.join();
}

struct ServerFixture {
  std::filesystem::path db_path;
  std::unique_ptr<Database> db;
  std::uint16_t port = 0;

  ServerFixture() {
    start_server_once(&port);
    // 每个用例一份新库：临时目录 + ULID，跑完即删
    db_path = std::filesystem::temp_directory_path() /
              ("genealogy-http-" + generate_ulid() + ".db");
    auto opened = Database::open(db_path);
    REQUIRE(opened.has_value());
    db = std::make_unique<Database>(std::move(*opened));
    REQUIRE(ensure_default_lineage(*db));
    bind_runtime(*db, false);   // 本地免鉴权模式
  }

  ~ServerFixture() {
    db.reset();
    std::error_code ec;
    std::filesystem::remove(db_path, ec);
    std::filesystem::remove(db_path.string() + "-wal", ec);
    std::filesystem::remove(db_path.string() + "-shm", ec);
  }
};

// 同步发一次请求，返回 (状态码, 响应体, 指定响应头)
struct Reply {
  int status = 0;
  int code = -100;   // drogon::ReqResult 的底层值；-100 = 未发送
  std::string body;
  std::string etag;
  std::string content_type;
  std::string acao;   // Access-Control-Allow-Origin（CORS 断言用）
};

Reply request(std::uint16_t port, drogon::HttpMethod method,
              const std::string& path, const std::string& body = "",
              const std::vector<std::pair<std::string, std::string>>&
                  headers = {}) {
  auto req = drogon::HttpRequest::newHttpRequest();
  req->setMethod(method);
  req->setPath(path);   // 只要路径；host/port 由 client 承载
  for (const auto& [k, v] : headers) req->addHeader(k, v);
  if (!body.empty()) {
    req->setBody(body);
    req->setContentTypeString("application/json; charset=utf-8");
  }
  Reply out;
  auto client = drogon::HttpClient::newHttpClient("127.0.0.1", port);
  // 同步发送：sendRequest(req, timeout) 返回 pair<ReqResult, HttpResponsePtr>
  // （内部用 promise/future 实现，但对外不是 future，别 .get()）。
  // 不能在 client 自己的事件循环线程里调，会死锁——测试线程满足这一点。
  const auto result = client->sendRequest(req, 10);
  out.code = static_cast<int>(result.first);
  if (result.first != drogon::ReqResult::Ok || result.second == nullptr) {
    out.status = -1;   // 让调用方的 REQUIRE 报出来，比静默返回 0 好排查
    return out;
  }
  const auto& resp = result.second;
  out.status = static_cast<int>(resp->getStatusCode());
  out.body = std::string(resp->body());
  out.etag = resp->getHeader("etag");
  out.content_type = resp->getHeader("content-type");
  out.acao = resp->getHeader("access-control-allow-origin");
  return out;
}

Reply get(std::uint16_t port, const std::string& path) {
  return request(port, drogon::Get, path);
}
Reply post(std::uint16_t port, const std::string& path,
           const std::string& body) {
  return request(port, drogon::Post, path, body);
}
Reply patch(std::uint16_t port, const std::string& path,
            const std::string& body,
            const std::vector<std::pair<std::string, std::string>>& h) {
  return request(port, drogon::Patch, path, body, h);
}

}  // namespace

// 测试跑完就停服。时机选 testRunEnded：此时 main 尚未返回，
// 早于一切静态析构——晚一步（比如靠静态对象析构）就会和 Drogon 的
// 单例析构抢顺序，那种崩法只能靠运气规避。
struct ServerShutdownListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;
  void testRunEnded(Catch::TestRunStats const&) override { stop_server(); }
};
CATCH_REGISTER_LISTENER(ServerShutdownListener)

// ================================================================ 用例

TEST_CASE_METHOD(ServerFixture, "HTTP：health 与 404 都走 JSON") {
  const auto h = get(port, "/api/v1/health");
  CHECK(h.status == 200);
  CHECK(nlohmann::json::parse(h.body)["status"] == "ok");

  // 404 也应是 problem+json，不是 Drogon 默认的 HTML
  const auto missing = get(port, "/api/v1/nope");
  CHECK(missing.status == 404);
  CHECK(missing.content_type.find("application/problem+json") !=
        std::string::npos);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：人物建档→详情→更新→删除 全链路") {
  const std::string lid = kDefaultLineageId;
  const std::string create =
      R"({"lineage_id":")" + lid + R"(","family_name":"陈","given_name":"文远",)"
      R"("gender":"male","generation_no":3})";
  const auto made = post(port, "/api/v1/persons", create);
  REQUIRE(made.status == 201);
  CHECK(made.etag == "\"1\"");
  const auto person = nlohmann::json::parse(made.body);
  const std::string id = person["id"].get<std::string>();
  CHECK(person["family_name"] == "陈");
  CHECK(person["given_name"] == "文远");
  CHECK(person["gender"] == "male");
  CHECK(person["rev"] == 1);

  // 详情
  const auto detail = get(port, "/api/v1/persons/" + id);
  REQUIRE(detail.status == 200);
  CHECK(detail.etag == "\"1\"");

  // 缺 If-Match 的 PATCH：428，不许盲写
  const auto no_match = patch(port, "/api/v1/persons/" + id,
                              R"({"generation_no":4})", {});
  CHECK(no_match.status == 428);

  // 带错版本号的 If-Match：409
  const auto stale = patch(port, "/api/v1/persons/" + id,
                           R"({"generation_no":4})", {{"If-Match", "\"99\""}});
  CHECK(stale.status == 409);
  const auto problem = nlohmann::json::parse(stale.body);
  CHECK(problem["code"] == 2009);
  CHECK(problem["status"] == 409);

  // 带正确版本号：200，rev 递增，ETag 跟着变
  const auto ok = patch(port, "/api/v1/persons/" + id,
                        R"({"generation_no":4})", {{"If-Match", "\"1\""}});
  REQUIRE(ok.status == 200);
  CHECK(ok.etag == "\"2\"");
  CHECK(nlohmann::json::parse(ok.body)["generation_no"] == 4);

  // 删除
  const auto gone = request(port, drogon::Delete, "/api/v1/persons/" + id);
  CHECK(gone.status == 204);
  CHECK(get(port, "/api/v1/persons/" + id).status == 404);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：参数校验失败给 problem+json + errors[]") {
  const auto bad = post(port, "/api/v1/persons", R"({"family_name":"陈"})");
  CHECK(bad.status == 400);
  const auto j = nlohmann::json::parse(bad.body);
  CHECK(j["code"] == 1001);
  CHECK(j["status"] == 400);
  CHECK(j["type"] ==
        "https://genealogy.local/problems/invalid-argument");
  REQUIRE(j.contains("errors"));
  CHECK(!j["errors"].empty());
  // 缺的字段要在 errors 里点名
  bool has_given = false;
  for (const auto& e : j["errors"]) {
    if (e["field"] == "given_name") has_given = true;
  }
  CHECK(has_given);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：列表分页游标可走完全表") {
  const std::string lid = kDefaultLineageId;
  // 灌 5 条，limit=2，应有三页且 next_cursor 逐步推进
  for (int i = 0; i < 5; ++i) {
    const auto r = post(port, "/api/v1/persons",
                        R"({"lineage_id":")" + lid +
                            R"(","family_name":"分页","given_name":")" +
                            std::to_string(i) + R"(","gender":"male"})");
    REQUIRE(r.status == 201);
  }

  int pages = 0;
  int seen = 0;
  std::string cursor;
  for (int guard = 0; guard < 10; ++guard) {
    std::string path = "/api/v1/persons?limit=2";
    if (!cursor.empty()) path += "&cursor=" + cursor;
    const auto page = get(port, path);
    REQUIRE(page.status == 200);
    const auto j = nlohmann::json::parse(page.body);
    seen += static_cast<int>(j["data"].size());
    ++pages;
    if (j["page"]["has_more"].get<bool>()) {
      cursor = j["page"]["next_cursor"].get<std::string>();
      REQUIRE(!cursor.empty());
    } else {
      break;
    }
  }
  CHECK(pages == 3);
  CHECK(seen == 5);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：宗族与地点的 CRUD") {
  const auto lin = post(port, "/api/v1/lineages",
                        R"({"name":"中湘陈氏","surname":"陈"})");
  REQUIRE(lin.status == 201);
  const auto lid = nlohmann::json::parse(lin.body)["id"].get<std::string>();

  const auto one = get(port, "/api/v1/lineages/" + lid);
  REQUIRE(one.status == 200);
  CHECK(nlohmann::json::parse(one.body)["name"] == "中湘陈氏");

  const auto upd = patch(port, "/api/v1/lineages/" + lid,
                         R"({"hall_name":"颍川堂"})", {{"If-Match", "\"1\""}});
  REQUIRE(upd.status == 200);
  CHECK(nlohmann::json::parse(upd.body)["hall_name"] == "颍川堂");

  const auto pl = post(port, "/api/v1/places",
                       R"({"name":"陈留","type":"village"})");
  REQUIRE(pl.status == 201);
  const auto pid = nlohmann::json::parse(pl.body)["id"].get<std::string>();
  CHECK(get(port, "/api/v1/places/" + pid).status == 200);

  // 地点类型不合法：400 且点名字段
  const auto badtype = post(port, "/api/v1/places",
                            R"({"name":"x","type":"planet"})");
  CHECK(badtype.status == 400);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：证据链（来源登记→字段级引用→删除保护 409）") {
  const std::string lid = kDefaultLineageId;

  // 建来源（嘉庆钞本——真实试修来源）
  const auto src = post(port, "/api/v1/sources",
                        R"({"type":"genealogy_book","title":"高阳李氏家谱",)"
                        R"("year_text":"嘉庆四年","repository":"上海图书馆"})");
  REQUIRE(src.status == 201);
  const std::string source_id =
      nlohmann::json::parse(src.body)["id"].get<std::string>();

  // 建人物并挂字段级引用（birth_date + locator 卷三 p136）
  const auto person = post(port, "/api/v1/persons",
                           R"({"lineage_id":")" + lid +
                               R"(","family_name":"陈","given_name":"证引",)"
                               R"("gender":"male"})");
  REQUIRE(person.status == 201);
  const std::string pid =
      nlohmann::json::parse(person.body)["id"].get<std::string>();

  const auto cit = post(port, "/api/v1/citations",
                        R"({"subject_type":"person","subject_id":")" + pid +
                            R"(","source_id":")" + source_id +
                            R"(","locator":"birth_date 卷三 p136",)"
                            R"("confidence":"high"})");
  REQUIRE(cit.status == 201);
  const std::string cit_id =
      nlohmann::json::parse(cit.body)["id"].get<std::string>();

  // 按主体过滤列表
  const auto listed =
      get(port, "/api/v1/citations?subject_type=person&subject_id=" + pid);
  REQUIRE(listed.status == 200);
  CHECK(nlohmann::json::parse(listed.body)["data"].size() == 1);

  // 来源被引用：删除来源 → 409
  CHECK(request(port, drogon::Delete, "/api/v1/sources/" + source_id)
            .status == 409);

  // 人物被引用：删除人物 → 409（ROADMAP M7.2 验收项）
  CHECK(request(port, drogon::Delete, "/api/v1/persons/" + pid).status == 409);

  // 解除引用 → 人物可删、来源可删（引用解除后 409 消失）
  CHECK(request(port, drogon::Delete, "/api/v1/citations/" + cit_id)
            .status == 204);
  CHECK(request(port, drogon::Delete, "/api/v1/persons/" + pid).status == 204);
  CHECK(request(port, drogon::Delete, "/api/v1/sources/" + source_id)
            .status == 204);
  CHECK(request(port, drogon::Delete, "/api/v1/sources/" + source_id)
            .status == 404);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：JSON 请求体含非法 UTF-8 得 400（不毒化进程）") {
  // 第三方客户端/老控制台发出 GBK 编码 body 曾直接把进程带到
  // 终止（parse 阶段，drogon 的 handler catch 都没机会跑）——
  // body_guard 在门口拦截，RFC 8259 本就要求 JSON 交换文本为 UTF-8。
  const std::string gbk = std::string("{\"lineage_id\":\"") + kDefaultLineageId +
                          std::string("\",\"family_name\":\"\xb3\xc2\",") +
                          std::string("\"gender\":\"male\"}");
  const auto bad = request(port, drogon::Post, "/api/v1/persons", gbk);
  CHECK(bad.status == 400);
  CHECK(bad.content_type.find("application/problem+json") !=
        std::string::npos);
  const auto j = nlohmann::json::parse(bad.body);
  CHECK(j["code"] == 1001);
  // 进程仍健康：后续请求照常
  CHECK(get(port, "/api/v1/health").status == 200);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：expand 展开子资源（events/unions/migrations）") {
  const std::string lid = kDefaultLineageId;
  const auto made = post(port, "/api/v1/persons",
                         R"({"lineage_id":")" + lid +
                             R"(","family_name":"陈","given_name":"展开",)"
                             R"("gender":"male"})");
  REQUIRE(made.status == 201);
  const std::string id =
      nlohmann::json::parse(made.body)["id"].get<std::string>();

  const auto spouse = post(port, "/api/v1/persons",
                           R"({"lineage_id":")" + lid +
                               R"(","family_name":"王","given_name":"氏",)"
                               R"("gender":"female"})");
  REQUIRE(spouse.status == 201);
  const std::string spouse_id =
      nlohmann::json::parse(spouse.body)["id"].get<std::string>();

  // 事件 + 婚姻 + 迁徙各挂一条
  REQUIRE(post(port, "/api/v1/events",
               R"({"subject_type":"person","subject_id":")" + id +
                   R"(","type":"burial","description":"葬祖茔"})")
              .status == 201);
  REQUIRE(post(port, "/api/v1/unions",
               R"({"type":"marriage","members":[{"person_id":")" + id +
                   R"(","role":"husband"},{"person_id":")" + spouse_id +
                   R"(","role":"wife"}]})")
              .status == 201);
  const auto place = post(port, "/api/v1/places",
                          R"({"name":"渝州","type":"county"})");
  REQUIRE(place.status == 201);
  const std::string place_id =
      nlohmann::json::parse(place.body)["id"].get<std::string>();
  REQUIRE(post(port, "/api/v1/migrations",
               R"({"subject_type":"person","subject_id":")" + id +
                   R"(","to_place_id":")" + place_id +
                   R"(","reason":"营商"})")
              .status == 201);

  // 不展开：响应里不应出现子资源字段
  const auto plain = get(port, "/api/v1/persons/" + id);
  REQUIRE(plain.status == 200);
  const auto plain_j = nlohmann::json::parse(plain.body);
  CHECK_FALSE(plain_j.contains("events"));
  CHECK_FALSE(plain_j.contains("unions"));
  CHECK_FALSE(plain_j.contains("migrations"));

  // 展开：三个子资源各一条
  const auto expanded =
      get(port, "/api/v1/persons/" + id + "?expand=events,unions,migrations");
  REQUIRE(expanded.status == 200);
  const auto j = nlohmann::json::parse(expanded.body);
  REQUIRE(j.contains("events"));
  CHECK(j["events"].size() == 1);
  CHECK(j["events"][0]["type"] == "burial");
  REQUIRE(j.contains("unions"));
  CHECK(j["unions"].size() == 1);
  REQUIRE(j.contains("migrations"));
  CHECK(j["migrations"].size() == 1);
  CHECK(j["migrations"][0]["reason"] == "营商");
}

TEST_CASE_METHOD(ServerFixture, "HTTP：按支派查迁徙分页不丢数据") {
  const std::string lid = kDefaultLineageId;
  const auto branch = post(port, "/api/v1/branches",
                           R"({"lineage_id":")" + lid +
                               R"(","name":"东八房"})");
  REQUIRE(branch.status == 201);
  const std::string bid =
      nlohmann::json::parse(branch.body)["id"].get<std::string>();

  const auto place = post(port, "/api/v1/places",
                          R"({"name":"渝州","type":"county"})");
  REQUIRE(place.status == 201);
  const std::string pid = nlohmann::json::parse(place.body)["id"].get<std::string>();

  auto make_person = [&](const std::string& given) {
    const auto r = post(port, "/api/v1/persons",
                        R"({"lineage_id":")" + lid + R"(","branch_id":")" +
                            bid + R"(","family_name":"陈","given_name":")" +
                            given + R"(","gender":"male"})");
    REQUIRE(r.status == 201);
    return nlohmann::json::parse(r.body)["id"].get<std::string>();
  };
  const std::string p1 = make_person("迁一");
  const std::string p2 = make_person("迁二");

  // 两个成员各挂 2 条迁徙（同一成员超过一页量，验证合并游标）
  for (const std::string& who : {p1, p2}) {
    for (int i = 0; i < 2; ++i) {
      const auto r = post(port, "/api/v1/migrations",
                          R"({"subject_type":"person","subject_id":")" + who +
                              R"(","to_place_id":")" + pid +
                              R"(","reason":"营商)" + std::to_string(i) +
                              R"("})");
      REQUIRE(r.status == 201);
    }
  }

  // limit=1 逐页走完全部 4 条（此前合并路径的游标口径会把后续页丢掉）
  int seen = 0;
  std::string cursor;
  for (int guard = 0; guard < 10; ++guard) {
    std::string path = "/api/v1/migrations?branch_id=" + bid + "&limit=1";
    if (!cursor.empty()) path += "&cursor=" + cursor;
    const auto page = get(port, path);
    REQUIRE(page.status == 200);
    const auto j = nlohmann::json::parse(page.body);
    seen += static_cast<int>(j["data"].size());
    if (j["page"]["has_more"].get<bool>()) {
      cursor = j["page"]["next_cursor"].get<std::string>();
      REQUIRE(!cursor.empty());
    } else {
      break;
    }
  }
  CHECK(seen == 4);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：世系算法（称谓/五服/祖先）") {
  const std::string lid = kDefaultLineageId;
  auto make = [&](const std::string& given, const std::string& gender,
                  const std::string& father, const std::string& mother) {
    std::string body = R"({"lineage_id":")" + lid + R"(","family_name":"陈",)"
                       R"("given_name":")" + given +
                       R"(","gender":")" + gender + "\"";
    if (!father.empty()) body += R"(,"father_id":")" + father + "\"";
    if (!mother.empty()) body += R"(,"mother_id":")" + mother + "\"";
    body += "}";
    const auto r = post(port, "/api/v1/persons", body);
    REQUIRE(r.status == 201);
    return nlohmann::json::parse(r.body)["id"].get<std::string>();
  };

  const std::string father = make("父", "male", "", "");
  const std::string mother = make("母", "female", "", "");
  const std::string son = make("子", "male", father, mother);

  // 称谓：子称父
  const auto app =
      get(port, "/api/v1/persons/" + son + "/appellation?to=" + father);
  REQUIRE(app.status == 200);
  const auto appj = nlohmann::json::parse(app.body);
  CHECK(appj["standard"] == "父亲");
  CHECK(appj["colloquial"] == "爸爸");
  // openapi 规定的 elder/path 也要在
  CHECK(appj["elder"] == true);
  CHECK(appj["path"] == nlohmann::json::array({"F"}));

  // 五服：子为父 = 斩衰（最重）
  const auto md = get(port, "/api/v1/persons/" + son +
                               "/mourning-degree?to=" + father);
  REQUIRE(md.status == 200);
  const auto mdj = nlohmann::json::parse(md.body);
  CHECK(mdj["garment"] == "zhancui");
  CHECK(mdj["duration_months"] == 27);
  CHECK(mdj["rule_id"] == "F");

  // 祖先链
  const auto anc = get(port, "/api/v1/persons/" + son + "/ancestors?depth=3");
  REQUIRE(anc.status == 200);
  const auto ancj = nlohmann::json::parse(anc.body);
  REQUIRE(ancj.contains("generations"));
  CHECK(ancj["generations"].size() >= 1);

  // 缺 to 参数：400
  CHECK(get(port, "/api/v1/persons/" + son + "/appellation").status == 400);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：检索与统计") {
  const std::string lid = kDefaultLineageId;
  const auto made = post(port, "/api/v1/persons",
                         R"({"lineage_id":")" + lid +
                             R"(","family_name":"陈","given_name":"德昭",)"
                             R"("gender":"male"})");
  REQUIRE(made.status == 201);

  // 两侧不能都是字面量：const char[] 相加会退化成指针相加（C2110）。
  // 显式把左侧提成 std::string。
  const auto found =
      get(port, std::string("/api/v1/search?q=") + "德昭");
  REQUIRE(found.status == 200);
  const auto j = nlohmann::json::parse(found.body);
  CHECK(j["data"].size() >= 1);

  const auto st = get(port, "/api/v1/stats");
  REQUIRE(st.status == 200);
  CHECK(nlohmann::json::parse(st.body)["person_count"].get<int>() >= 1);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：校验端点返回问题清单") {
  const std::string lid = kDefaultLineageId;
  // 造一个时间悖论：出生晚于卒年
  const auto bad = post(port, "/api/v1/persons",
                        R"({"lineage_id":")" + lid +
                            R"(","family_name":"陈","given_name":"悖论",)"
                            R"("gender":"male","birth_date":{)"
                            R"("gregorian":"1900-01-01"},"death_date":{)"
                            R"("gregorian":"1850-01-01"}})");
  REQUIRE(bad.status == 201);
  const std::string bad_id =
      nlohmann::json::parse(bad.body)["id"].get<std::string>();

  const auto v = post(port, "/api/v1/validate", "{}");
  REQUIRE(v.status == 200);
  const auto j = nlohmann::json::parse(v.body);
  CHECK(j.contains("issues"));
  CHECK(j["summary"]["error"].get<int>() >= 1);

  // M7.3 R16：挂上 citation 后该人物不再出现在无证据清单
  bool bad_in_r16 = false;
  {
    const auto v16 = post(port, "/api/v1/validate", R"({"rule_ids":["R16"]})");
    REQUIRE(v16.status == 200);
    const auto j16 = nlohmann::json::parse(v16.body);
    CHECK(j16["summary"]["info"].get<int>() >= 1);  // 新库大部分主体无引用
    for (const auto& i : j16["issues"]) {
      if (i["entity_id"] == bad_id) bad_in_r16 = true;
    }
    CHECK(bad_in_r16);
  }
  const auto src = post(port, "/api/v1/sources",
                        R"({"type":"genealogy_book","title":"高阳李氏测试谱"})");
  REQUIRE(src.status == 201);
  const std::string src_id =
      nlohmann::json::parse(src.body)["id"].get<std::string>();
  const auto cite = post(
      port, "/api/v1/citations",
      R"({"subject_type":"person","subject_id":")" + bad_id +
          R"(","source_id":")" + src_id + R"("})");
  REQUIRE(cite.status == 201);
  {
    const auto v16 = post(port, "/api/v1/validate", R"({"rule_ids":["R16"]})");
    REQUIRE(v16.status == 200);
    const auto j16 = nlohmann::json::parse(v16.body);
    for (const auto& i : j16["issues"]) {
      CHECK(i["entity_id"].get<std::string>() != bad_id);  // 已有证据，不再报
    }
  }

  // M7.3 R17：同字段两条不同来源 high 引用 → 裁定项；disputed 后消失
  const auto src2 = post(port, "/api/v1/sources",
                         R"({"type":"oral","title":"族老口述"})");
  REQUIRE(src2.status == 201);
  const std::string src2_id =
      nlohmann::json::parse(src2.body)["id"].get<std::string>();
  const auto c1 = post(
      port, "/api/v1/citations",
      R"({"subject_type":"person","subject_id":")" + bad_id +
          R"(","source_id":")" + src_id +
          R"(","locator":"birth_date","confidence":"high"})");
  REQUIRE(c1.status == 201);
  const auto c2 = post(
      port, "/api/v1/citations",
      R"({"subject_type":"person","subject_id":")" + bad_id +
          R"(","source_id":")" + src2_id +
          R"(","locator":"birth_date","confidence":"high"})");
  REQUIRE(c2.status == 201);
  const auto v17 = post(port, "/api/v1/validate", R"({"rule_ids":["R17"]})");
  REQUIRE(v17.status == 200);
  CHECK(nlohmann::json::parse(v17.body)["summary"]["warning"].get<int>() >= 1);

  // 裁定：将其中一条置为 disputed（confidence PATCH）→ 冲突消除
  const std::string c1_id = nlohmann::json::parse(c1.body)["id"].get<std::string>();
  const auto patched =
      patch(port, "/api/v1/citations/" + c1_id,
            R"({"confidence":"disputed"})", {});
  REQUIRE(patched.status == 200);
  const auto v17b = post(port, "/api/v1/validate", R"({"rule_ids":["R17"]})");
  REQUIRE(v17b.status == 200);
  CHECK(nlohmann::json::parse(v17b.body)["summary"]["warning"].get<int>() == 0);
}

TEST_CASE_METHOD(ServerFixture, "HTTP：JWT 签发可自校验") {
  // auth 本身是纯函数，这里顺带验一遍签发/校验闭环
  const std::string token =
      server::auth::issue_token("test", server::auth::Role::Editor);
  server::auth::Claims claims;
  std::string err;
  REQUIRE(server::auth::verify_token(token, &claims, &err));
  CHECK(claims.username == "test");
  CHECK(claims.role == server::auth::Role::Editor);

  // 改一个字节应当验不过
  std::string tampered = token;
  tampered.back() = (tampered.back() == 'A') ? 'B' : 'A';
  server::auth::Claims c2;
  CHECK_FALSE(server::auth::verify_token(tampered, &c2, &err));
}

TEST_CASE_METHOD(ServerFixture, "HTTP：require-auth 下数据端点必须持有效令牌") {
  bind_runtime(*db, true, "s3cret");

  // 匿名访问数据端点：401 problem+json（带 WWW-Authenticate 语义）
  const auto anon = get(port, "/api/v1/stats");
  CHECK(anon.status == 401);
  CHECK(anon.content_type.find("application/problem+json") !=
        std::string::npos);
  // 401 是 pre-routing 短路响应，不走 postHandling advice——CORS 头
  // 必须由守卫自己补，否则跨端口本机来源下是浏览器 opaque response。
  const auto anon_cors = request(port, drogon::Get, "/api/v1/stats", "",
                                 {{"Origin", "http://127.0.0.1:5173"}});
  CHECK(anon_cors.status == 401);
  CHECK(anon_cors.acao == "http://127.0.0.1:5173");
  // 伪造令牌：401
  const auto forged = request(port, drogon::Get, "/api/v1/stats", "",
                              {{"Authorization", "Bearer not.a.jwt"}});
  CHECK(forged.status == 401);
  // SSE 变更流同样在守卫范围内（pre-routing 在路由查找之前，未注册
  // 路径也是 401 而非 404）
  CHECK(get(port, "/api/v1/events/stream").status == 401);
  // 匿名白名单：健康探针照常可达
  CHECK(get(port, "/api/v1/health").status == 200);

  // 令牌签发：缺 password → 400（点名缺哪个字段）；错口令 → 401
  const auto no_pw = post(port, "/api/v1/auth/token", R"({"username":"u"})");
  CHECK(no_pw.status == 400);
  const auto j = nlohmann::json::parse(no_pw.body);
  bool pw_named = false;
  for (const auto& e : j["errors"]) {
    if (e["field"] == "password") pw_named = true;
  }
  CHECK(pw_named);
  const auto bad_pw = post(port, "/api/v1/auth/token",
                           R"({"username":"u","password":"nope"})");
  CHECK(bad_pw.status == 401);

  // 对口令 → 200，令牌可访问数据端点
  const auto good_pw = post(port, "/api/v1/auth/token",
                            R"({"username":"u","password":"s3cret"})");
  REQUIRE(good_pw.status == 200);
  const std::string jwt =
      nlohmann::json::parse(good_pw.body)["access_token"].get<std::string>();
  const auto ok = request(port, drogon::Get, "/api/v1/stats", "",
                          {{"Authorization", "Bearer " + jwt}});
  CHECK(ok.status == 200);

  bind_runtime(*db, false, "");   // 还原本地免鉴权，防串扰后续用例
}

TEST_CASE_METHOD(ServerFixture, "HTTP：CORS 默认只放行本机来源") {
  // 第三方来源：响应不带 Access-Control-Allow-Origin（浏览器据此拦截
  // 对 127.0.0.1 的跨站读写——「绑回环」挡不住来自受害者浏览器的请求）
  const auto evil = request(port, drogon::Get, "/api/v1/health", "",
                            {{"Origin", "http://evil.example"}});
  REQUIRE(evil.status == 200);
  CHECK(evil.acao.empty());
  // 预检同样不放行（204 但无允许头，浏览器不会发出实际请求）
  const auto pre_evil =
      request(port, drogon::Options, "/api/v1/persons", "",
              {{"Origin", "http://evil.example"},
               {"Access-Control-Request-Method", "POST"}});
  CHECK(pre_evil.status == 204);
  CHECK(pre_evil.acao.empty());
  // 本机来源（跨端口开发，如 Vite 5173 → 8080）：默认放行
  const auto local = request(port, drogon::Get, "/api/v1/health", "",
                             {{"Origin", "http://127.0.0.1:5173"}});
  REQUIRE(local.status == 200);
  CHECK(local.acao == "http://127.0.0.1:5173");
  const auto local_name = request(port, drogon::Get, "/api/v1/health", "",
                                  {{"Origin", "http://localhost:3000"}});
  CHECK(local_name.acao == "http://localhost:3000");
}
