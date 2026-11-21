# clients/web（Web UI 参考实现）

TypeScript 客户端参考实现，用于验证 REST API 的外接能力（设计文档 2.2 L4 客户端层）。分三层，职责不混：

- **类型层**：`src/api/types.ts` 由 [openapi-typescript](https://openapi-types.dev/)
  从 `shared/openapi/openapi.yaml` 生成（**勿手改**）。spec 是唯一事实源，
  改类型先改规范；规范校验见仓库根 `tools/validate_openapi.py`。
- **运行层**：`src/api/client.ts` 几十行薄封装——拼 URL、Bearer 头、
  RFC 9457 problem+json 错误、If-Match/rev 乐观锁、游标分页迭代器。
  不引第三方 HTTP 库（fetch 在 Node 18+ 与浏览器里都是全局的）。
- **界面层**：`src/views/*` React 单页应用（Vite 构建，无组件库，
  样式手写于 `src/styles.css`）。

## 命令

```bash
npm install          # 首次
npm run generate     # spec → src/api/types.ts
npm run typecheck    # tsc --noEmit
npm run smoke        # client.ts 读写闭环（自拉临时 server，26 项断言）
npm run build        # 生成类型 + 生产构建 → dist/
npm run dev          # 开发服务器（/api 代理到 127.0.0.1:8080）
npm run ui-smoke     # 浏览器端 UI 闭环（playwright，12 项断言）
```

`smoke` 与 `ui-smoke` 都会自建临时库、自选端口、跑完自清理，
不碰任何真实数据；依赖 `build/bin/genealogy-server.exe` 已构建。

## M5 闭环（已验收）

浏览器端「建档 → 关系 → 世系图 → 导出」全部跑通（`ui-smoke` 18 项断言，含窄视口 3 项）：
连接配置 → 宗族建档 → 人物建档/编辑（If-Match 乐观锁）→ 婚姻登记 →
人物卡（父母/婚姻/事件/五服/称谓）→ 世系图 SVG（BFS 分层 + 婚姻虚线）→
GEDCOM/DOT 导出下载 → 校验视图。CORS 已由 server 的
`install_cors`（`server/src/cors.cpp`）提供，跨端口开发直连。

## 进度

- M5-1/M5-2：Web UI 闭环 ✅（18 项浏览器断言全绿）
- M5-3：SSE 变更通知 ✅（实时条 + ui-smoke「实时条收到 person.created」断言；第三轮新-W4 对齐——此前仍写「规划中」）
- M5-4：迁徙登记 + SVG 迁徙地图 ✅（「迁徙」视图完整可用；同上对齐——此前仍写「占位说明中」）

本目录为独立前端工程，不参与 C++ 构建与 CI 的 C++ 矩阵；
`ui-smoke` 走 CI 的 Windows job（playwright 为 devDependency）。
