-- M8.1：Idempotency-Key 重放缓存（设计文档 5.2.1：POST 24h 内重放
-- 返回首次结果）。选库表持久化（ROADMAP「内存 + 库表择一」）——
-- 重启后重放仍正确，且无需内存淘汰管理；过期条目由写入方惰性清理
-- （每次 INSERT 前 DELETE 24h 前的旧条目）。
-- 只缓存 2xx：失败响应不缓存（客户端可修正后重试）。
CREATE TABLE idempotency_cache (
  key        TEXT PRIMARY KEY,
  status     INTEGER NOT NULL,
  body       TEXT NOT NULL,
  created_at INTEGER NOT NULL  -- Unix 秒（24h 窗口数值比较）
);
