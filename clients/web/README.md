# clients/web（M0 占位）

Web UI 参考实现（TypeScript + React + 由 `shared/openapi.yaml` 生成的客户端），
用于验证 REST API 的外接能力（设计文档 2.2 L4 客户端层）。

- M5 实现「建档 → 关系 → 世系图 → 导出」浏览器闭环；
- 开发时指向本地服务：`genea serve --port 8080`（M2 提供）；
- 本目录为独立前端工程，不参与 C++ 构建与 CI 的 C++ 矩阵。
