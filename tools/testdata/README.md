# 全过程测试数据集

覆盖 genealogy 四端（CLI / REST / Web / GUI 数据层）的全功能测试数据与执行脚本。
设计原则：**一份数据只测一类行为，断言打在每个功能的真实出口上**（退出码、JSON 字段、响应头）。

## 数据文件

| 文件 | 内容 | 测什么 |
|---|---|---|
| `family_gedcom.ged` | 四代陈氏主谱：11 人、4 结合（含 `_CONC` 妾室）、7 事件、多行 NOTE、REFN、SOUR/OBJE 记录 | GEDCOM 导入全标签路径、地点自动创建、损失显式化（SOUR/OBJE）、世系算法（父子/曾祖孙/同父异母）、导出往返、DOT/Mermaid 渲染 |
| `broken_gedcom.ged` | 逐条触发 R1/R2/R3/R5/R12/R13 的脏数据 | 校验规则命中（各规则的 error/warning/info 出口）、全量 validate 退出码 1 |
| `cycle_gedcom.ged` | 世系环：自指（F900）+ 互环（F901/F902） | 环导入被明确拒绝（报「世系环」+ 环上人名），不挂起、不报误导性的「父亲不存在」 |
| `build_cases.sh` | CLI 建 `cases.db`，触发 R6/R7/R8/R9/R10/R14 | CLI 建档路径的规则触发（同性结合、父母冗余不一致、缺性别/生年/世代、排行断层） |

## 执行脚本

```bash
# 一键跑三层（在仓库根执行）
bash tools/testdata/run_all.sh

# 或单独跑
bash tools/testdata/run_e2e.sh    # CLI 端到端：57 项断言
bash tools/testdata/http_e2e.sh   # REST 端到端：30 项断言（自起 server，端口 18099）
```

`run_e2e.sh` 覆盖：建库 → 建档 → GEDCOM 导入 → 校验 → 查询 → 世系算法 →
十代树 → 导出往返 → 树渲染 → 历法工具（1583–3000）→ 字辈 → 迁徙 → 事件 →
灌库（seed）→ 备份恢复（含拒绝覆盖）→ 脏数据规则命中 → 环拒绝 → CLI 规则集。

`http_e2e.sh` 覆盖：health / openapi 托管 / 人物 CRUD / **ETag + If-Match 乐观锁
（428 缺头、409 错版本、200 通过）** / FTS 检索 / 世系端点（ancestors、descendants、
tree、mourning-degree、appellation）/ validate / stats / **404 problem+json** /
**SSE change 帧实时推送** / DELETE 204。

## 已知环境坑（脚本内已规避）

- Git Bash 的 `curl` 是 cygwin 构建，`-w '%{http_code}'` 会丢花括号 → 状态码一律
  从响应头首行解析；Windows 原生 curl 走系统代理，连 127.0.0.1 会 502，勿用。
- MSYS 管道 + 原生 exe 偶发丢字节 → `pid_of` 带重试。
- `build_cases.sh` 只清自己的 `cases.db`，绝不 `rm -rf` 整个 `.data/testdata`
  （曾因此清掉 run_e2e 的全部产物）。
- 分页 envelope 的数组 key 是 `data`（page_envelope），不是 `items`。

## 修复记录

2026-10-23 本轮测试发现并修复 3 个软件缺陷（详见 `REPORT.md`），均已补回归单测：
`core/tests/test_validation.cpp`（R7 单方父母误报）、
`core/tests/test_generation_poem.cpp`（poem check 跨宗族混入）、
`core/tests/test_gedcom.cpp`（世系环导入报错误导）。
