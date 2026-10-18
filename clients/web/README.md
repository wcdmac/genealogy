# clients/web（Web UI 参考实现的客户端工程）

TypeScript 客户端参考实现，用于验证 REST API 的外接能力（设计文档 2.2 L4 客户端层）。
分两层，职责不混：

- **类型层**：`src/api/types.ts` 由 [openapi-typescript](https://openapi-types.dev/)
  从 `shared/openapi/openapi.yaml` 生成（**勿手改**）。spec 是唯一事实源，
  改类型先改规范；规范校验见仓库根 `tools/validate_openapi.py`。
- **运行层**：`src/api/client.ts` 几十行薄封装——拼 URL、Bearer 头、
  RFC 9457 problem+json 错误、If-Match/rev 乐观锁、游标分页迭代器。
  不引第三方 HTTP 库（fetch 在 Node 18+ 与浏览器里都是全局的）。

## 命令

```bash
npm install          # 首次（devDependencies：openapi-typescript + typescript）
npm run generate     # spec → src/api/types.ts
npm run typecheck    # tsc --noEmit
npm run smoke        # 读写闭环冒烟（自动拉起临时 server，26 项断言）
```

`npm run smoke` 依赖 `build/bin/genealogy-server.exe` 已构建；
它会自建临时库、自选端口，跑完自动清理，不碰任何真实数据。

## 冒烟覆盖的读写闭环

宗族建档 → 三代人物 → 婚姻 → 事件 → PATCH+409 乐观锁 → 五服/称谓 →
检索/统计/校验 → GEDCOM/DOT 导出 → 软删除后 404 → 分页迭代。

## 进度

- M2：类型生成 + 薄封装 + 冒烟全部就绪（26/26 通过）。
- M5：Web UI 本体（React 界面）与 spec 中 `x-roadmap` 规划的端点族
  （迁徙、证据链、附件、SSE）随 core 领域模型补齐后接入。

本目录为独立前端工程，不参与 C++ 构建与 CI 的 C++ 矩阵。
