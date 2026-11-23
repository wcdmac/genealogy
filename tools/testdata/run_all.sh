#!/usr/bin/env bash
# run_all.sh —— 两层测试一键执行：单测 → CLI 端到端
# 任一失败即停（set -e -o pipefail），全部通过退出码 0，可直接当 CI 卡点。
# pipefail 必须带：单测输出经管道进 tail，没有它管道退出码取自 tail
# （恒 0），单测挂了也照样打 ALL_LAYERS_PASS——假卡点比没有更危险。
# 用法：bash tools/testdata/run_all.sh   （在仓库根执行）
set -e -o pipefail
cd "$(dirname "$0")/../.."

# 平台自适应后缀：Windows 是 .exe，Linux/macOS 无后缀
EXE=""
if [ "$(uname -s)" = "MINGW"* ] || [ "$(uname -s)" = "MSYS"* ] || [ "$(uname -s)" = "CYGWIN"* ]; then
  EXE=".exe"
fi

echo "########## 1/2 单元测试（core + gui） ##########"
"./build/bin/genealogy-core-tests${EXE}" 2>&1 | tail -1
"./build/bin/genealogy-gui-tests${EXE}" 2>&1 | tail -1

echo
echo "########## 2/2 CLI 端到端 ##########"
bash tools/testdata/run_e2e.sh

echo
echo "ALL_LAYERS_PASS"
