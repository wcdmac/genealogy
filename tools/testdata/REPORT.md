# 全过程测试报告

- **日期**：2026-10-23
- **范围**：基于自建全功能测试数据集（`tools/testdata/`），对 genealogy 四端执行三层测试
- **结论**：**发现并修复 3 个软件缺陷 + 8 处测试脚本自身缺陷；终态全绿**（core 786234 断言 / 112 用例，server 104 断言 / 9 用例，CLI 端到端 57 项 × 多轮，HTTP 端到端 30 项 × 多轮）

## 一、执行矩阵

| 层级 | 命令 | 结果 |
|---|---|---|
| core 单测 | `genealogy-core-tests.exe` | All passed（786234 assertions / 112 test cases） |
| server 单测 | `genealogy-server-tests.exe` | All passed（104 assertions / 9 test cases） |
| CLI 端到端 | `bash tools/testdata/run_e2e.sh` | PASS=57 FAIL=0（稳定性连跑 3 轮） |
| HTTP 端到端 | `bash tools/testdata/http_e2e.sh` | PASS=30 FAIL=0（稳定性连跑 2 轮） |
| 一键三层 | `bash tools/testdata/run_all.sh` | 全部通过 |

## 二、发现并修复的软件缺陷（3 个，均已补回归单测）

### 缺陷 1：R7 对「仅登记一方父母」的人物全员误报 error

- **现象**：GEDCOM 主谱导入后 `io validate` 报 1 条 error——妾室（`_CONC`）结合所生的女儿，只有 `father_id`（FAM 无 WIFE 时母亲不写冗余字段），R7 却判「找不到同时包含二人的婚姻结合」。
- **根因**：`core/src/validate/validator.cpp` `check_parent_link_inconsistent` 在 `m`（母亲）为空时仍走配偶互链检查，`linked` 必然为 false。单亲、妾室未录母亲、续弦未录是谱书常见形态，全部中招。
- **修复**：互链检查仅在 `f`、`m` 均非空时进行；「父/母指向图外」的检查保持原样。
- **守护**：`test_validation.cpp` 新增用例「R7 仅登记一方父母不误报」（仅父/仅母不报、双方登记无结合照旧报）。
- **实验实锤**：CLI 建「仅 --father」人物，`validate --rule R7` 复报 error。

### 缺陷 2：poem check（字辈校验）跨宗族混入，误报淹真实信号

- **现象**：`genea poem check --lineage L` 在只有 11 人的主谱上报 53 处「辈字与世代冲突」，冲突名单里大量出现 seed 灌库人物（属于另一个宗族）。
- **根因**：`core/src/service/generation_poem_service.cpp` `check_consistency` 的 `PersonFilter` 不过滤归属，`persons_.list()` 拉全库（代码注释自述「一期简化为全库」）。`--lineage` 只决定用哪份派语，不决定检查哪些人。
- **修复**：lineage 归属按 `pf.lineage_id` 过滤；branch 归属按 `pf.branch_id` 过滤（一期不含子支派，与原注释一致）。
- **效果**：53 处假冲突 → 0；剩余 2 处是测试数据自身辈字与派语不匹配（数据问题，已把派语改成与谱中辈字名对齐的「德文承世，源远流长」）。
- **守护**：`test_generation_poem.cpp` 新增用例「校验只查本宗族人物，别族数据不混入」。

### 缺陷 3：世系环导入报误导性错误「父亲不存在」

- **现象**：含互环（甲乙互为父子）的 GEDCOM 导入失败，报 `建人物失败(陈互环甲): not_found: 父亲不存在: 01J...`——病因是环导致依赖排序排不出，报错却指向「人不存在」，且整份文件（含 13 个无辜人物）被事务整体回滚。
- **根因**：`core/src/io/gedcom.cpp` `order_by_dependency` 对环按 id 硬灌（注释声明的设计行为），落库时才由用例层报 not_found。
- **修复**：映射层落库前新增 `find_ancestry_cycle()`（显式栈 DFS + safe 剪枝，双亲边都走，不漏「仅经母亲边可达」的环），发现环即返回 `invalid_argument("世系环，无法导入: <环上人名>（父母链成环）")`。
- **守护**：`test_gedcom.cpp` 新增用例「世系环拒绝并点名，不报误导性的『父亲不存在』」（互环 + 自指 + 无环正常谱不受影响）。

## 三、发现并修复的测试脚本缺陷（8 处，工具自身问题）

1. `build_cases.sh` 开头 `rm -rf .data/testdata` 把 `run_e2e.sh` 的全部产物清空 → 改为只删自己的 `cases.db`。
2. `run_e2e.sh` `jval()` 用 `$2` 取键（管道模式下实为 `$1`）→ 修正。
3. `db seed --persons 200` 实际生成 211 人（按世代铺满略超目标），断言硬编码 211 → 改为动态 `11 + seed.json 的 persons`（并计入往返导入的 11 人）。
4. 「导入未挂起」断言写反（拿 124 当期望值，没挂起反而 FAIL）→ 改为 `ok/hung` 语义。
5. R3 触发数据设计错：规则判据是「子出生年 − 父卒年 > 80」，误按「− 父出生年」设计 → 改数据（父 1750 生/1805 卒，子 1890 生）。
6. `http_e2e.sh` 用 cygwin curl 的 `-w '%{http_code}'`（该构建丢花括号，输出字面量）→ 状态码改从响应头首行解析。
7. `http_e2e.sh` 分页 envelope key 写成 `items`，实际是 `data` → FTS/search 命中统计修正。
8. SSE 断言时序抖动（change 帧已到但 kill 早于判定）→ 轮询等帧落盘再 kill。
9. `test_gedcom.cpp` 新用例误用 `Result::value_or`（非 optional）→ 改 `has_value()` + 解引用；`test_validation.cpp` 新用例 `ps.back()` 索引错位 → 改 `ps[1]`。

## 四、修复过程验证

- 三处 core 修复均先经**最小实验实锤**再动手（如仅登记父亲触发 R7 的 10 秒探针库）。
- 每轮修复后全量回归：单测（core/server）+ CLI 端到端 + HTTP 端到端，无一项回归。
- 端到端连跑 3 轮、HTTP 连跑 2 轮确认稳定性（曾遇 1 次 MSYS 管道丢字节导致的间歇 FAIL，已用重试 + JSON 断言加固）。

## 五、复跑方式

```bash
# 在仓库根
bash tools/testdata/run_all.sh        # 三层一键
bash tools/testdata/run_e2e.sh        # 仅 CLI 层
bash tools/testdata/http_e2e.sh       # 仅 HTTP 层
```

数据集构成与每个文件测什么，见 `tools/testdata/README.md`。
