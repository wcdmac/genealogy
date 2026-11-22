# 全过程测试数据集

覆盖 genealogy 两端（CLI 端到端 + GUI 无头单测）的功能测试数据与执行脚本。
项目已按单机单谱单库定位裁撤 REST/Web 层；原 `http_e2e.sh` 已删除。
设计原则：**一份数据只测一类行为，断言打在每个功能的真实出口上**（退出码、JSON 字段）。

## 数据文件

| 文件 | 内容 | 测什么 |
|---|---|---|
| `family_gedcom.ged` | 四代陈氏主谱：11 人、4 结合（含 `_CONC` 妾室）、7 事件、多行 NOTE、REFN、SOUR/OBJE 记录 | GEDCOM 导入全标签路径、地点自动创建、损失显式化（SOUR/OBJE）、世系算法（父子/曾祖孙/同父异母）、导出往返、DOT/Mermaid 渲染 |
| `broken_gedcom.ged` | 逐条触发 R1/R2/R3/R5/R12/R13 的脏数据 | 校验规则命中（各规则的 error/warning/info 出口）、全量 validate 退出码 1 |
| `cycle_gedcom.ged` | 世系环：自指（F900）+ 互环（F901/F902） | 环导入被明确拒绝（报「世系环」+ 环上人名），不挂起、不报误导性的「父亲不存在」 |
| `build_cases.sh` | CLI 建 `cases.db`，触发 R6/R7/R8/R9/R10/R14 | CLI 建档路径的规则触发（同性结合、父母冗余不一致、缺性别/生年/世代、排行断层） |

## 执行脚本

```bash
# 一键跑（在仓库根执行；Windows 本机）
bash tools/testdata/run_all.sh

# 或单独跑 CLI 端到端（CI 三平台亦执行此脚本，77 项断言）
bash tools/testdata/run_e2e.sh
```

CI 三平台通过 `GENEA_BIN` 环境变量传入各平台 CLI 产物路径；`run_e2e.sh`
与 `build_cases.sh` 均支持该变量，本机默认 `./build/bin/genea.exe`。
`run_e2e.sh` 覆盖：建库 → 建档 → GEDCOM 导入 → 校验 → 查询 → 世系算法 →
十代树 → 导出往返 → 树渲染 → 历法工具（1368–3000）→ 字辈 → 迁徙 → 事件 →
灌库（seed）→ 备份恢复（含拒绝覆盖）→ 脏数据规则命中 → 环拒绝 → CLI 规则集 →
名号域 → 文档检索 → bench 不污染用户库。

GUI 无头单测由 `ctest` 的 `genealogy-gui-tests` 目标覆盖（DbSession 调用链 +
面板状态机），渲染层的显示环境判定由 `genealogy-gui --headless-check` 验证。

## 已知环境坑（脚本内已规避）

- Windows Git Bash + 原生 exe 经管道偶发丢字节 → `pid_of` 带重试。
- `run_timeout()` 优先 GNU `timeout`，其次 `gtimeout`，macOS 两者都没有时直接执行
  （macOS runner 默认无 GNU coreutils，裸 `timeout` 会 command not found）。
- `build_cases.sh` 只清自己的 `cases.db`，绝不 `rm -rf` 整个 `.data/testdata`
  （曾因此清掉 run_e2e 的全部产物）。
- 产物留在 `.data/testdata`（gitignore），供失败时排查；下次运行开头会自清。

## 历史记录

`REPORT.md` 为 2026-10 阶段的历史报告，含已裁撤的 REST/Web 层描述，
仅作沿革参考；当前覆盖以本 README 与 CI 步骤为准。
