# genealogy（家谱管理软件）

跨平台家谱管理软件。**核心 C++20**，桌面 GUI 采用 Dear ImGui + GLFW（**严禁 Qt**），
对外 REST API（OpenAPI 3.1）+ CLI，第三方客户端可经 API 外接。

设计文档（调研结论 / 架构 / 模块划分 / 数据模型 / 接口规范）位于仓库外：
`D:/cd/tree/genealogy-design/家谱管理软件-调研与架构设计.md`（v1.0，2026-10-12）。

## 当前状态（M1.6）

- [x] monorepo 骨架 + CMake + vcpkg manifest
- [x] core：领域模型（person/lineage/branch/place/union/event）/ GenealogyDate 多历法值对象 / ULID / SQLite 仓储 + 迁移 / 用例层（五服务，含领域校验）
- [x] cli：`db init|seed` / `person add|list|show|relation|tree` / `lineage|branch|place|union|event add|list|show|remove` / `date convert|lunar|terms|era|info` / `io import|export|validate` / `bench`
- [x] **M1.3 世系算法引擎**：世系图快照 + BFS 关系路径 + 世代序号（自始祖起算）+ 辈分差 + 五服（明清定型版）+ 称谓；规则表数据外置（`shared/rules/*.json`，构建时嵌入）
- [x] **M1.4 历法引擎**：公历↔农历互转（**1583-3000**，1418 个农历年）/ 24 节气时刻（北京时间，到分）/ 干支纪年纪月纪日 / 生肖 / 年号纪年（西汉建元至民国，499 条区间）
- [x] **M1.5 GEDCOM 5.5.1 导入导出 + 一致性校验规则引擎**：三层解析（字符集/结构/映射）、
  255 字节 CONC 切行、妾与继配用 `_CONC` 扩展标签保留；15 条校验规则全部外置在
  `shared/rules/validation.json`，阈值/等级/启停改 JSON 即生效
- [x] **M1.6 性能灌库验收**：合成谱生成器（固定种子可复现）+ 灌库快路径 + `genea bench`
  基准报告（10 万人物 / 8.9 万事件，全部达标，余量 4～12 倍）+ 规模往返哨兵测试
- [x] Catch2 测试（784547 断言 / 81 用例）+ 三平台 CI + 许可证白名单卡点
- [ ] M2：REST 服务（Drogon）；M3：GUI（Dear ImGui）

### 历法引擎的数据从哪来

农历不是算着玩的，得有权威基准。做法是先取官方日历、逐条核验，再落表：

| 项 | 说明 |
|---|---|
| 基准 | 中国科学院紫金山天文台《1900-2025年日历》PDF（380 页），逐日对照表 + 页脚节气表 |
| 核验 | 逐日 46021 天 **0 处不符**；节气 3024 条时刻一致（官方取分、四舍五入导致的 47 条 1 分钟差已按官方值覆盖） |
| 口径 | 月首取定朔（北京时间）；十一月含冬至、无中气之月置闰（GB/T 33661-2017） |
| 覆盖 | **1583-3000**。下界取 1583 的原因：1582 年格里历改革删 10 天，且更早年份存在月长 28 天 / 月序错乱等推算异常，宁缺勿滥 |
| 旁证 | 闰月占比 36.9%（理论 36.8%）；全区间逐年自洽（正月初一落在 1/21–2/21、节气严格递增、冬至必在十一月） |
| 年号 | cn-era（MIT，朝代编号取自 CBDB）+ opencc 繁转简；按《中国历史纪年表》口径订正：宣统止于 1911、民国纪年止于 1949；明清民国元年均有独立锚点回归 |

数据生成链路见 `tools/gen_calendar/`（离线跑一次，产物是 C++ 静态表，运行时零依赖）。

### 一致性校验规则集

规则表在 `shared/rules/validation.json`，构建期嵌入；**阈值、等级、文案、是否启用都在 JSON 里，
换规则不改代码**。判定逻辑是代码里固定的一组谓词（规则表的 `check` 字段即谓词名）——
没做成通用表达式语言：家谱校验要的是可读、可测试、可回溯，一个小型 DSL 只会把规则文件
变成没人敢改的第二套程序。

| 规则 | 查什么 | 等级 |
|---|---|---|
| R1 | 出生晚于卒年 | error |
| R2 | 父母与子女出生年相距不足 12 年 | error |
| R3 | 子女出生时母亲已逾 55 岁 / 父亲已逾 80 岁 | warning |
| R4 | 父母指向自己 | error |
| R5 | 同名且生年相同（疑似重复人物） | warning |
| R6 | 配偶性别相同且均已登记 | warning |
| R7 | `father_id`/`mother_id` 冗余字段找不到对应的婚姻结合 | error |
| R8 | 有父母但未标世代序号 | info |
| R9 | 性别未登记 | info |
| R10 | 既无公历生年也无原文生年 | info |
| R11 | 沿父母链上溯回到自己（世系图有环） | error |
| R12 | 日期有原文但换算不出公历 | info |
| R13 | 墓葬事件早于卒年 | warning |
| R14 | 同胞排行序号不连续 | warning |
| R15 | 无备注、无行传、无出处（**默认停用**，证据普查时再开） | info |

时间类规则只认公历年份：农历原文换算不出公历就跳过（并由 R12 单独提示），
**宁可少报，不可拿不可靠的年份去判悖论**。`io validate` 有 error 时退出码为 1，可直接当 CI 卡点。

## 目录结构

```
genealogy/
├── core/             # libgenealogy-core：领域核心（无 UI、无网络依赖）
├── cli/              # genea 命令行
├── server/           # REST 服务（Drogon，BUILD_SERVER=ON 启用）
├── gui/              # 桌面 GUI（Dear ImGui + GLFW，BUILD_GUI=ON 启用）
├── clients/web/      # Web UI 参考实现（TS + React）
│   ├── schema/       # SQLite DDL 与迁移脚本（构建时嵌入二进制）
│   ├── rules/        # 五服/称谓/校验规则表（JSON，构建时嵌入；换规则不改代码）
│   ├── licenses/     # 依赖许可证白名单
│   └── openapi/      # OpenAPI 规范（客户端代码生成唯一事实源）
├── tools/
│   ├── gen_calendar/ # 历法/年号数据表离线生成器 + 官方基准 fixtures
│   └── license_check.py 等工程工具
```

## 构建（Windows / MSVC）

本机 PATH 中含有 MSYS2 的 32 位 g++（会抢编译器），且 reg.exe 在安全策略
黑名单中（vcvars64.bat 依赖它定位 Windows SDK，不可用）。因此 Windows 上
**必须用仓库自带的构建脚本**：自动拼装 MSVC x64 环境、锁定 cl 编译器、
经 vcpkg manifest 装依赖、构建并跑测试：

```bash
cmd /c tools\build-msvc.bat
```

等价手工流程（vcpkg + Ninja + MSVC，需先备好 MSVC 环境变量）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build
ctest --test-dir build --output-on-failure
```

macOS / Linux 将 vcpkg 路径换成本机位置即可（CI 已覆盖三平台）。
Linux 需 `ninja-build`；macOS 用 `arm64-osx` 三元组。

### 本机环境备忘（2026-10-12 实测）

| 事项 | 结论 |
|---|---|
| vcpkg | 装在 `D:/vcpkg`，baseline `e4563094…`，manifest 模式自动装依赖 |
| Windows SDK | 实际在 `D:\Windows Kits\10`（**不在** `C:\Program Files (x86)`） |
| 源码编码 | 全仓 UTF-8，MSVC 已全局加 `/utf-8`（否则中文按 GBK 误读，连环语法错误） |
| vcpkg 端口 | 带 `.rc` 的端口（如 spdlog）在 VS2026 + 中文语言包下触发 RC1107，故 spdlog/fmt 延后到 M1 |
| SQLite | 官方 amalgamation 经 FetchContent 自建（`third_party/sqlite3`），不用 vcpkg 端口 |
| `.bat` 脚本 | 必须 CRLF 行尾；含 `(x86)` 的路径不能进 `if exist … (` 块条件（cmd 解析坑） |
| 沙箱 | vcpkg/ninja 子进程在沙箱内被 stdio 管道拦截（error 231），构建需沙箱外执行 |
| 脚本入口 | 若 `cmd.exe` 被安全策略拦截：按脚本第 87–90 行手工设 `PATH`/`INCLUDE`/`LIB`（MSVC bin + SDK bin + ucrt/um/shared 头与库）后直调 cmake 即可，效果等价 |

启用可选组件：

```bash
cmake -S . -B build -DBUILD_SERVER=ON -DBUILD_GUI=ON ...
```

## 使用示例

```bash
./build/bin/genea db init --db family.db

# 宗族 / 支派 / 地点
./build/bin/genea lineage add --db family.db --name "中湘陈氏六修族谱" \
  --surname 陈 --commandery 颍川 --hall 德星堂
./build/bin/genea branch add --db family.db --lineage 01J... --name 东八房 --gen-char 明
./build/bin/genea place add --db family.db --name 中湘 --type county --modern "湖南省湘潭县"

# 人物（--lineage 缺省落入占位宗族）
./build/bin/genea person add --db family.db \
  --family-name 陈 --given-name 文远 --gender male \
  --birth "同治三年四月初四"          # 老谱原文照录，calendar=lunar
./build/bin/genea person add --db family.db \
  --family-name 陈 --given-name 文远 --gender male \
  --birth 1864-05-09                 # ISO 日期，calendar=gregorian，自动推年干支

# 婚姻结合（--member 人物ID:角色，多人逗号分隔）与事件
./build/bin/genea union add --db family.db --type marriage \
  --member "01J...:husband,01K...:wife" --start "光绪十二年"
./build/bin/genea event add --db family.db --subject-type person --subject 01J... \
  --type burial --date "光绪二十七年春" --description "葬祖茔亥山巳向"

./build/bin/genea person list --db family.db --format json
./build/bin/genea person show --db family.db 01J...

# 历法换算（纯计算，不碰数据库）
./build/bin/genea date info                     # 覆盖区间与编算口径
./build/bin/genea date convert 1864-05-09
# 年号 清·同治三年
# 公历 1864-05-09
# 农历 甲子年（1864）四月初四
# 纪年 甲子年 · 鼠 · 月 己巳 · 日 甲戌
./build/bin/genea date lunar --year 2025 --month 6 --day 20 --leap   # 闰六月二十 → 2025-08-13
./build/bin/genea date terms --year 2025 --index 4                    # 惊蛰 2025-03-05 16:07
./build/bin/genea date era 同治三年              # → 1864 年（甲子年 · 鼠），正月初一 1864-02-08
./build/bin/genea date era 天顺元年              # 重名年号报冲突并列出候选：元天顺=1328 明天顺=1457

# GEDCOM 导入导出与校验（走数据库，与 API 同一条用例层路径）
./build/bin/genea lineage add --db family.db --name 中湘陈氏 --surname 陈   # 先有宗族
./build/bin/genea io import gedcom 族谱.ged --db family.db --lineage 01J... [--surname 陈]
# 导入完成: 人物 5 · 结合 2 · 事件 6 · 地点 1
# 映射损失: 1 条（warning 0 条，info 1 条）
#   [I] @S1@ SOUR — 证据引用未建模（M1 无 Source/Citation），出处链已丢失
./build/bin/genea io validate --db family.db
# 规则集 genealogy-validation v1（跑了 14/15 条）
# 合计: error 0 · warning 1 · info 2
#   [warning] R5 person 01J... — 与另一人同名且生年相同（确认是同一人误录两条，还是同名的两个人）
./build/bin/genea io export gedcom --db family.db --out 导出.ged --submitter 谱师

# 性能验收：灌库 + 跑基准（M1.6）
./build/bin/genea db seed --db family.db --persons 100000     # 合成谱灌库
./build/bin/genea person tree <始祖ID> --db family.db --depth 10
./build/bin/genea bench --db family.db --persons 100000      # 退出码 0 = 全达标
# 指标                       目标          实测        结果
# 灌库 100000 人物 + 88739 事件 < 60000       13667.0 ms   PASS
# FTS 检索（10 万级）          < 50             5.2 ms   PASS
# 十代子孙树（77816 节点/10 层）< 200          17.8 ms   PASS
# 世系图快照构建（100000 人物） 参考项         303.7 ms   PASS
# GEDCOM 导出 10000 人物        < 10000        49.0 ms   PASS
# GEDCOM 导入 10000 人物        < 60000      7709.7 ms   PASS

# 世系算法：两人关系路径 / 辈分差 / 五服 / 称谓
./build/bin/genea person relation --db family.db 01J... 01K...
# 关系路径: H.F
# 世代差: -1（尊长 1 辈）
# 世代序号: 己 第 1 世 · 彼 第 3 世（序号差 2）
# 五服[ming_qing]: 斩衰 三年（27 月） — 妇为舅姑（明清斩衰三年）
# 称谓: 公公（口语: 公公；书面: 夫父）
```

> **年号换算的岁首陷阱**：年号元年记的是「正月所在公历年」，而农历年的冬月、腊月
> 会落到公历次年——**光绪三十四年冬月初一 = 1908-11-24**（仍是光绪三十四年，不在宣统）。
> `date era` 只解到「年」，要精确到日必须再过一遍农历引擎（`date lunar`）。

> **两个"代"的口径要分清**（设计文档 4.4.2）：
> - **世代差（辈分差）** = 关系路径上的世代升降次数：上行 -1、下行 +1、配偶步不升降。
>   夫妻同辈、妻之公公 -1。这是称谓与五服的依据。
> - **世代序号** = 自始祖起算的第几世（大宗谱法），用于字辈与统计。
>   外亲、姻亲常因父母未录而上溯断链，序号会塌到始祖辈（上例"己 第 1 世"），
>   **不可**当作辈分差使用。

退出码：0 成功 / 1 运行时错误 / 2 用法错误 / 3 不存在 / 4 版本冲突。

> **性能口径**：`bench` 的每一项都对着设计文档的验收线，退出码 0 表示全达标，
> 可直接当 CI 卡点。合成谱用固定种子（`--seed`），同参数永远长出同一份数据，
> 数字可复现、可对比。生成的家谱保持世系常理：每代年龄递增、婚姻早于生育、
> 子女出生不早于父母 + 最小生育年龄、`generation_no` 逐代 +1、行几连续。
> 户主性别决定配偶性别与结合角色（儿子娶妻、女儿招婿），不产出同性结合——
> 否则 R6 会被大规模误报，把真要人看的信号淹掉。

> **GEDCOM 换算的三条硬规矩**：
> - **不猜**。DATE 解不出公历（`INT 1900 (say)`、`30 FEB 1900`）就只留原文 + 记损失，
>   绝不编一个日期出来；ANSEL / MacRoman 字符集直接拒收并提示先转 UTF-8。
> - **损失显式化**。OBJE（附件）、SOUR（证据链）、_UID、ASSO 都会逐条列出来，
>   谱师能看清「原文里有、我们库里没有」的到底是哪些。
> - **妾与继配不归并**。GEDCOM 5.5.1 没有这个角色，导出用扩展标签 `_CONC` 保留，
>   别的软件会忽略它而不是读错；亲子关系只认「夫妻双方齐全」的结合，
>   孩子该挂正室还是侧室是谱学判断，程序不替谱师决定。

## 依赖与许可

核心链路全部为宽松许可（MIT / BSD / zlib / BSL / 公有领域），无 Copyleft 成分，
可静态链接、可闭源分发。白名单由 CI 卡点强制执行（`tools/license_check.py`）。

`tools/gen_calendar/` 用到三个 Node 包（lunar-javascript / cn-era / opencc-js，均 MIT 或
MIT+Apache-2.0），但**只用于离线生成 C++ 静态表**：不进 `vcpkg.json`、不参与编译链接、
不随二进制分发。详见 `shared/licenses/allowlist.txt` 的登记说明。
