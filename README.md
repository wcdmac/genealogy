# genealogy（家谱管理软件）

跨平台家谱管理软件。**核心 C++20**，桌面 GUI 采用 Dear ImGui + GLFW（**严禁 Qt**），
对外 REST API（OpenAPI 3.1）+ CLI，第三方客户端可经 API 外接。

设计文档（调研结论 / 架构 / 模块划分 / 数据模型 / 接口规范）位于仓库外：
`D:/cd/tree/genealogy-design/家谱管理软件-调研与架构设计.md`（v1.0，2026-10-12）。

## 当前状态（M1.3）

- [x] monorepo 骨架 + CMake + vcpkg manifest
- [x] core：领域模型（person/lineage/branch/place/union/event）/ GenealogyDate 多历法值对象 / ULID / SQLite 仓储 + 迁移 / 用例层（五服务，含领域校验）
- [x] cli：`db init` / `person add|list|show|relation` / `lineage|branch|place|union|event add|list|show|remove`
- [x] **M1.3 世系算法引擎**：世系图快照 + BFS 关系路径 + 世代序号（自始祖起算）+ 辈分差 + 五服（明清定型版）+ 称谓；规则表数据外置（`shared/rules/*.json`，构建时嵌入）
- [x] Catch2 测试（555 断言 / 27 用例）+ 三平台 CI + 许可证白名单卡点
- [ ] M1.4：历法引擎（农历/年号互转）
- [ ] M1.4：历法引擎（农历/年号互转）
- [ ] M1.5：GEDCOM 导入导出 + 校验规则引擎
- [ ] M1.6：性能灌库验收
- [ ] M2：REST 服务（Drogon）；M3：GUI（Dear ImGui）

## 目录结构

```
genealogy/
├── core/             # libgenealogy-core：领域核心（无 UI、无网络依赖）
├── cli/              # genea 命令行
├── server/           # REST 服务（Drogon，BUILD_SERVER=ON 启用）
├── gui/              # 桌面 GUI（Dear ImGui + GLFW，BUILD_GUI=ON 启用）
├── clients/web/      # Web UI 参考实现（TS + React）
├── shared/
│   ├── schema/       # SQLite DDL 与迁移脚本（构建时嵌入二进制）
│   ├── rules/        # 五服 / 称谓规则表（JSON，构建时嵌入；换规则不改代码）
│   ├── licenses/     # 依赖许可证白名单
│   └── openapi/      # OpenAPI 规范（客户端代码生成唯一事实源）
└── tools/            # license_check.py 等工程工具
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

# 世系算法：两人关系路径 / 辈分差 / 五服 / 称谓
./build/bin/genea person relation --db family.db 01J... 01K...
# 关系路径: H.F
# 世代差: -1（尊长 1 辈）
# 世代序号: 己 第 1 世 · 彼 第 3 世（序号差 2）
# 五服[ming_qing]: 斩衰 三年（27 月） — 妇为舅姑（明清斩衰三年）
# 称谓: 公公（口语: 公公；书面: 夫父）
```

> **两个"代"的口径要分清**（设计文档 4.4.2）：
> - **世代差（辈分差）** = 关系路径上的世代升降次数：上行 -1、下行 +1、配偶步不升降。
>   夫妻同辈、妻之公公 -1。这是称谓与五服的依据。
> - **世代序号** = 自始祖起算的第几世（大宗谱法），用于字辈与统计。
>   外亲、姻亲常因父母未录而上溯断链，序号会塌到始祖辈（上例"己 第 1 世"），
>   **不可**当作辈分差使用。

退出码：0 成功 / 1 运行时错误 / 2 用法错误 / 3 不存在 / 4 版本冲突。

## 依赖与许可

核心链路全部为宽松许可（MIT / BSD / zlib / BSL / 公有领域），无 Copyleft 成分，
可静态链接、可闭源分发。白名单由 CI 卡点强制执行（`tools/license_check.py`）。
