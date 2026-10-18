// SSE 实时变更条（M5-3）。
//
// 订阅 client.subscribe，保留最近几条变更显示在顶栏下。
// 断线 3s 自动重订（与 server 初始帧的 retry: 3000 同节奏）——
// 本地总线不重放历史，重连后靠各视图的 list 端点自然补偿。

import { useEffect, useState } from "react";

import { useClient } from "../context";
import type { EventNotice } from "../api/client";

export function EventFeed() {
  const { client, connection } = useClient();
  const [events, setEvents] = useState<EventNotice[]>([]);
  const [live, setLive] = useState(false);

  useEffect(() => {
    if (!connection.connected) {
      setLive(false);
      return;
    }
    let disposed = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const start = () => {
      if (disposed) return;
      const unsubscribe = client.subscribe(
        (notice) => {
          setLive(true);
          setEvents((previous) => [notice, ...previous].slice(0, 5));
        },
        () => {
          setLive(false);
          unsubscribe();
          timer = setTimeout(start, 3000);
        },
      );
    };
    start();

    return () => {
      disposed = true;
      if (timer) clearTimeout(timer);
    };
  }, [client, connection.connected]);

  return (
    <div className="event-feed">
      <span className={live ? "badge badge-ok" : "badge"}>
        {live ? "实时协同 · 在线" : "实时协同 · 重连中"}
      </span>
      {events.map((notice, index) => (
        <span key={`${notice.at ?? ""}-${index}`} className="event-item">
          {notice.type} · {notice.subject_type}/
          {(notice.subject_id ?? "").slice(0, 8)}
          {notice.rev != null ? ` · rev=${notice.rev}` : ""}
        </span>
      ))}
      {events.length === 0 && (
        <span className="muted">暂无变更（任一端的写操作会实时推到这里）</span>
      )}
    </div>
  );
}
