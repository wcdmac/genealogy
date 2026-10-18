#pragma once

// SSE 事件总线（M5-3，设计文档 5.4「多端协同」）。
//
// 形态：写操作（建档/改/删）成功后向总线 publish 一条变更通知，
// 所有挂着的 SSE 连接（GET /events/stream）实时收到——
// 多端 GUI 与 Web 客户端因此保持协同。本地单机模式同样可用
// （只有本机能连，开了无害）。
//
// 为什么是内存总线而不是落库：事件的意义就是「刚刚发生了」，
// 迟一分钟读到还不如重新 GET；断线重连后的补偿由客户端的
// 游标分页自行完成（list 端点本来就是事实源）。
//
// 连接生命周期：drogon 的 newAsyncStreamResponse 回调把 stream 的
// 所有权（unique_ptr）交给本总线，故这里持强引用保活；断开靠
// send() 返回 false 判定并移除（drogon 不提供断开回调）。
// 15s 的 ping 帧顺带保证了「从未发布事件的长连接」也会被及时清理。

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace genealogy::server {

class EventBus {
 public:
  static EventBus& instance();

  // 安装全局 ping 定时器（15s 一条注释帧，防代理/浏览器断空闲连接）
  void install(drogon::HttpAppFramework& app);

  // SSE 连接登记（持有强引用保活；发送失败时自动移除）
  void subscribe(std::shared_ptr<drogon::ResponseStream> stream);

  // 广播一条变更。type 形如 person.updated / union.created；
  // rev 对无版本表的实体（event/place）留空。
  void publish(const std::string& type,
               const std::string& subject_type,
               const std::string& subject_id,
               std::optional<std::int64_t> rev);

  std::size_t connection_count() const;

 private:
  EventBus() = default;

  void broadcast(const std::string& frame);

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<drogon::ResponseStream>> streams_;
};

// 便捷宏：写操作成功后发一条通知。放在各路由 handler 的成功分支，
// 比在每个 service 里埋钩子小——service 不该知道 HTTP 的存在（ADR-1）。
#define GENEALOGY_PUBLISH_CHANGE(type, subject_type, subject_id, rev) \
  ::genealogy::server::EventBus::instance().publish(                 \
      (type), (subject_type), (subject_id), (rev))

}  // namespace genealogy::server
