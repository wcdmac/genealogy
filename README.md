# genealogy（家谱管理软件）

**单机家谱管理软件**（2026-11-21 定位收口）。**核心 C++20**，桌面 GUI 采用
Dear ImGui + GLFW（**严禁 Qt**）+ CLI 三端共用同一领域核心；**无任何联网组件**。
家谱按「**单谱单库**」运营：一个数据库文件承载一部家谱，不同家的谱各开各的库。

设计文档（调研结论 / 架构 / 模块划分 / 数据模型 / 接口规范）位于仓库外：
`D:/cd/tree/genealogy-design/家谱管理软件-调研与架构设计.md`（v1.0，2026-10-12）。

## 当前状态（M6）

- [x] monorepo 骨架 + CMake + vcpkg manifest
- [x] core：领域模型（person/lineage/branch/place/union/event/migration）/ GenealogyDate 多历法值对象 / ULID / SQLite 仓储 + 迁移 / 用例层
- [x] cli：`db init|seed|backup|restore` / `person add|list|show|name|relation|tree|remove` / `book show|update` / `branch|place|union|event|migration|source|citation|document add|list|show|remove`（各命令组以实际帮助为准） / `poem set|show|check` / `date convert|lunar|terms|era|info` / `io import gedcom|export gedcom|export tree|export book|validate` / `bench`
- [x] **M1.3 世系算法引擎**：世系图快照 + BFS 关系路径 + 世代序号（自始祖起算）+ 辈分差 + 五服（明清定型版）+ 称谓；规则表数据外置（`shared/rules/*.json`，构建时嵌入）
- [x] **M1.4 历法引擎**：公历↔农历互转（**1368-3000**，1633 个农历年；1368-1644 为大统历朔闰史实段，1368-1582 儒略口径、1583-1644 格里口径）/ 24 节气时刻（北京时间，到分，1645 起）/ 干支纪年纪月纪日 / 生肖 / 年号纪年（西汉建元至民国，499 条区间）
- [x] **M1.5 GEDCOM 5.5.1 导入导出 + 一致性校验规则引擎**：三层解析（字符集/结构/映射）、
  255 字节 CONC 切行、妾与继配用 `_CONC` 扩展标签保留；16 条校验规则全部外置在
  `shared/rules/validation.json`，阈值/等级/启停改 JSON 即生效
- [x] **M1.6 性能灌库验收**：合成谱生成器（固定种子可复现）+ 灌库快路径 + `genea bench`
  基准报告（10 万人物 / 8.9 万事件，全部达标，余量 4～12 倍）+ 规模往返哨兵测试
- [x] **单机定位收口（2026-11-21）**：原 M2 REST 服务 / M2 TS 客户端 / M5 Web UI+SSE
  随「单机软件、单谱单库」整体裁撤（`server/`、`clients/` 源码已从工作树移除，
  git 历史可完整恢复）。无联网组件。
- [x] **M3 桌面 GUI（Dear ImGui docking + GLFW，MIT/zlib）**：`genealogy-gui` 起 docking 主窗口，
  人物卡（行内编辑 + rev 乐观锁）/ 世系表（可排序大表格）/ 检索（FTS）/ 关系（路径+辈分+五服+称谓）/
  世系树（十代展开）/ 校验面板；OpenGL 3.2+/GLSL 130，中文字体按平台探测，
  高对比度皮肤 + 字体缩放；`--headless-check` 供 CI 区分「无显示环境」与「真故障」
- [x] **M4 算法与互通**：五服/称谓/世代/字辈引擎（派语解析/辈字定位/世代一致性，CLI `genea poem`）+
  五服规则表快照测试 + GEDCOM 双向（往返零严重损失）
- [x] **M5 迁徙与导出**：迁徙登记 + SVG 迁徙地图；世系图 DOT/Mermaid 导出；
  单谱单库后迁徙载体按 book 记录
- [x] **M6 发行**：三平台打包（CPack：Windows ZIP / macOS dmg / Linux tar.gz，
  Windows DLL 全量随包）+ 文档站（`docs/`：使用手册 + 修谱工作流）+
  备份恢复（`genea db backup|restore`，VACUUM INTO 一致性快照；演练 20 项断言（含旁车失败回滚注入），CI 卡点）
- [x] **M7 数据可信度（进行中）**：证据链（source/citation 全生命周期，三端；删除被引事实 409）+
  校验 R16 无证据事实清单 / R17 引用冲突裁定（confidence=disputed 出口）+
  Document 域（谱序/凡例/族规家训/艺文/契约；正文 trigram FTS 检索，中文子串可命中）+
  GEDCOM SOUR 双向映射（0 SOUR 记录 + INDI/FAM/事实级引用 + QUAY↔置信度，往返零损失）+
  名号变更史（person_name：谱名/乳名/学名/改后名/身后名，起止 + 主名全人唯一）+
  在世者按隐私级别脱敏（本地库由 GUI 显式开关控制）；原 M8 云端安全
  （ACL/幂等/限流/audit）随 REST 裁撤一并关闭
- [x] **P1 世系图（2026-11-23）**：`genea io export chart` 六种体例（祖先图 pedigree / 后代图 descendant / 沙漏图 hourglass / 五服图 wufu / 欧式 european / 苏式 soviet），
  纯 C++ 自生成 SVG（规避 Graphviz EPL 依赖），内置焦点高亮、折叠计数、搜索定位、房支分色、缺失祖先/成环显式告警；五服图逐对调 core 规则引擎算服等
- [x] **P0 出版与字体（2026-11-23）**：`genea io export book` 三种版式（世系表/世系谱/行传）
  × A4/16 开 × 纵横向，生成自包含 HTML（内嵌完整思源宋体，浏览器 Ctrl+P 另存 PDF，竖排+页码+派语）；
  随包完整思源黑体/宋体 SC（OFL 1.1，56MB，`bin/fonts/`，GUI 优先使用），不做子集化以免缺字
- [x] Catch2 测试（core / CLI / GUI 三层，单谱单库契约）+ 三平台 CI
  （构建/测试/打包/CLI e2e/备份演练/GUI 无头自检）+ 许可证白名单卡点

### 历法引擎的数据从哪来

农历不是算着玩的，得有权威基准。做法是先取官方日历、逐条核验，再落表：

| 项 | 说明 |
|---|---|
| 基准 | 中国科学院紫金山天文台《1900-2025年日历》PDF（380 页），逐日对照表 + 页脚节气表 |
| 核验 | 逐日 46021 天 **0 处不符**；节气 3024 条时刻一致（官方取分、四舍五入导致的 47 条 1 分钟差已按官方值覆盖） |
| 口径 | 月首取定朔（北京时间）；十一月含冬至、无中气之月置闰（GB/T 33661-2017） |
| 覆盖 | **1368-3000**。1368-1582 为大统历朔闰史实段（固化历法事实数据，含明实录订正），按**儒略历史实口径**读写（1582-10-15 改革前民用日期即儒略历；1582-10-05~14 改革缺口报非法日期）；1583-1644 同为大统历史实段，按**格里历口径**读写；1645-3000 为现代天文推算段（清《时宪书》改定气定朔，与现行推算同源）。1368 年以前不提供，越界显式报错。节气数据自 **1645** 年起 |
| 旁证 | 闰月占比 36.9%（理论 36.8%）；1645-3000 逐年自洽（正月初一落在 1/21–2/21、节气严格递增、冬至必在十一月）；1368-1644 逐月逐日往返（277 年）+ 明实录三锚点（卷29「乙亥即位」= 儒略 1368-01-23 = 正月初四、卷143「闰二月乙巳」= 儒略 1382-04-09 = 闰二月廿五） |
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
| R16 | 无证据事实：人物/事件没有任何 citation 挂接（主体级口径，locator 可字段级精化） | info |
| R17 | 同一事实存在多条不同来源的 high 置信引用（互斥嫌疑，**人工裁定队列**：对照原文后一致合并、互斥置 disputed） | warning |

> R15（无备注/行传/出处的启发式）于 M7.3 移除，被 R16 的 citation 实测取代；编号留缺口不重排。

时间类规则只认公历年份：农历原文换算不出公历就跳过（并由 R12 单独提示），
**宁可少报，不可拿不可靠的年份去判悖论**。`io validate` 有 error 时退出码为 1，可直接当 CI 卡点。

## 目录结构

```
genealogy/
├── core/             # libgenealogy-core：领域核心（无 UI、无网络依赖）
├── cli/              # genea 命令行
├── gui/              # 桌面 GUI（Dear ImGui + GLFW，BUILD_GUI=ON 启用）
├── shared/
│   ├── schema/       # SQLite DDL 与迁移脚本（构建时嵌入二进制）
│   ├── rules/        # 五服/称谓/校验规则表（JSON，构建时嵌入；换规则不改代码）
│   └── licenses/     # 依赖许可证白名单
├── docs/             # 文档站：使用手册 + 修谱工作流
├── tools/
│   ├── gen_calendar/    # 历法/年号数据表离线生成器 + 官方基准 fixtures
│   ├── vcpkg-install.ps1    # 本机安装 vcpkg 依赖（绕 reg.exe 黑名单/MSYS2 抢编译/并发死锁）
│   ├── build-msvc.bat        # 构建 core+cli（Windows 一条龙）
│   ├── build-gui-msvc.bat    # 构建 core+gui 全家桶
│   ├── drill_backup_restore.py  # 备份恢复演练（CI 卡点）
│   ├── license_check.py      # 许可证白名单卡点（含禁 Qt）
│   └── vcpkg-triplets/      # overlay triplet：只编 Release
└── .github/workflows/ci.yml  # 三平台 CI
```

## 文档站

`docs/` 下有完整文档（Markdown，GitHub 直接渲染；`docs/index.html` 为静态入口）：

- [使用手册](docs/manual/使用手册.md)：安装（三平台包 / 源码构建）、CLI、桌面 GUI、字辈引擎、备份恢复
- [修谱工作流](docs/manual/修谱工作流.md)：从筹备（收谱/访谈/定派语）到成谱（校验/字辈/导出/备份）的完整步骤与常见坑

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
cmake -S . -B build -DBUILD_GUI=ON ...
```

## 使用示例

```bash
./build/bin/genea db init --db family.db

# 谱元数据（单谱单库：db init 已建 book 行）/ 支派 / 地点
./build/bin/genea book update --db family.db --name "中湘陈氏六修族谱" \
  --surname 陈 --commandery 颍川 --hall 德星堂
./build/bin/genea branch add --db family.db --name 东八房 --gen-char 明
./build/bin/genea place add --db family.db --name 中湘 --type county --modern "湖南省湘潭县"

# 人物
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
genea book update --db family.db --name 中湘陈氏 --surname 陈   # 单谱单库：设置谱元数据
./build/bin/genea io import gedcom 族谱.ged --db family.db [--surname 陈]
# 导入完成: 人物 5 · 结合 2 · 事件 6 · 地点 1
# 映射损失: 1 条（warning 0 条，info 1 条）
#   SOUR 已映射为 source/citation；0 条 SOUR 记录，往返零损失
./build/bin/genea io validate --db family.db
# 规则集 genealogy-validation v1（16 条规则全跑；R15 已移除留缺口）
# 合计: error 0 · warning 1 · info 2
#   [warning] R5 person 01J... — 与另一人同名且生年相同（确认是同一人误录两条，还是同名的两个人）
./build/bin/genea io export gedcom --db family.db --out 导出.ged --submitter 谱师
# 导出侧损失台账：凡「库里有、GEDCOM 表达不了/不在导出集内」的信息
# 逐条打到 stderr（[W]/[I]），摘要行带损失计数，json 模式含 losses 字段。
# 全量导出应为损失 0；--lineage/--limit 部分导出时缺集引用会逐条记账。

# 性能验收：灌库 + 跑基准（M1.6）
./build/bin/genea db seed --db family.db --persons 100000     # 合成谱灌库
./build/bin/genea person tree <始祖ID> --db family.db --depth 10
./build/bin/genea bench --db family.db --persons 100000      # 退出码 0 = 全达标
# 指标                       目标          实测        结果
# 灌库 100000 人物 + 300000 事件 < 60000       13667.0 ms   PASS
# FTS 检索（10 万级）          < 50             5.2 ms   PASS
# 十代子孙树（77816 节点/10 层）< 200          17.8 ms   PASS
# 世系图快照构建（100000 人物） 参考项         303.7 ms   PASS
# GEDCOM 导出 10000 人物        < 10000        49.0 ms   PASS
# GEDCOM 导入 10000 人物        < 60000      7709.7 ms   PASS

# REST / Web UI 已于 2026-11-21 随单机定位裁撤（git 历史可恢复）。
# 单机用法：genea db init → person add → io validate → db backup。

# 迁徙（M5）
./build/bin/genea migration add --db family.db --subject-type person \
  --subject 01J... --from 01P... --to 01Q... --reason 营商 --route "中湘 → 渝州"
./build/bin/genea migration list --db family.db --subject 01J...

# 字辈（M4）
./build/bin/genea poem set --db family.db --text "德茂光先祖，文章振万邦"
./build/bin/genea poem check --db family.db   # 辈字/世代一致性，退出码 1 = 有不一致
# 派语不从一世起用时必须传 --start N（N=派语首句对应的世代）——真实谱
# 常见「某世起才有派语」，缺省 start=1 会把整支误报成冲突（试修实证）。
# 支派各有派语时用 --branch <支派ID> 按支校验。

# 备份恢复（M6）
./build/bin/genea db backup --db family.db --out backup/     # 一致性快照（VACUUM INTO）
./build/bin/genea db restore --db family.db --from backup/   # 恢复（目标已存在需 --force）
python3 tools/drill_backup_restore.py                        # 完整演练（灌库→备份→删库→恢复→核对）

# 桌面 GUI（M3）
tools\build-gui-msvc.bat                        # 构建 core+cli+gui（单机三目标）
./build/bin/genealogy-gui --db family.db        # 带库启动
./build/bin/genealogy-gui --headless-check      # CI/SSH 自检：0=有显示 2=无显示
# 面板：人物卡 / 世系表 / 检索 / 关系 / 世系树 / 校验（菜单「面板」开关）。
# 数据层与渲染层分层：DbSession 调用链（GUI 走 core 用例的同一入口）
# 无 ImGui 依赖，由 genealogy-gui-tests 无头单测；面板状态机（*_state.cpp）
# 也已编入该目标直接驱动。渲染层要显示环境，CI 只验 --headless-check。

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
不随二进制分发。1368-1644 大统历段是固化的历法事实数据（各月初一落在哪一天，
依《著作权法》第五条第（三）项历法不受著作权保护；见
`tools/gen_calendar/fixtures/ming_datong_facts.json`），不含任何上游代码或表达。
详见 `shared/licenses/allowlist.txt` 的登记说明。
