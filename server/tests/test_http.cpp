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
#include <genealogy/server/routes_entities.hpp>
#include <genealogy/server/routes_misc.hpp>
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
    // 不注册 pre-routing advice：库句柄与鉴权开关由 bind_runtime 单例
    // 提供（db_from/require_auth_of 都会退到进程级），与 main.cpp 同一条
    // 路径——测试与生产走同一条取用链，才不会出现「测试过、线上挂」。
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
    ensure_default_lineage(*db);
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

  const auto v = post(port, "/api/v1/validate", "{}");
  REQUIRE(v.status == 200);
  const auto j = nlohmann::json::parse(v.body);
  CHECK(j.contains("issues"));
  CHECK(j["summary"]["error"].get<int>() >= 1);
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
