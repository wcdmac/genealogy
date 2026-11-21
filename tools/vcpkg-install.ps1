# 在本机装 vcpkg 依赖（Drogon 等）。
#
# 为什么需要这个脚本而不是直接 `vcpkg install`：
#   本机有三条硬约束（均为 2026-10-14 实测）：
#   ① cmd.exe 与 reg.exe 在 WorkBuddy 安全策略的程序黑名单上，vcvars64.bat
#      与 vcpkg 自带的编译器/SDK 探测都要调它们，直接用会被拦；
#   ② PATH 里 D:\DevTool\msys64\mingw32\bin 有 32 位 cmake/g++，会抢编译器；
#   ③ vcpkg 默认把环境变量剥干净再传给构建子进程，手工设好的
#      PATH/INCLUDE/LIB 传不进去。
#
#   解法同 tools/build-msvc.bat：手工设 MSVC + Windows SDK 环境（绕开 ①），
#   整个换掉 PATH 只留必要目录（绕开 ②），再用 VCPKG_KEEP_ENV_VARS
#   让 vcpkg 别剥掉它们（绕开 ③）。
#
#   缺 ③ 的现象：端口在编译器自检时报 `rc ... no such file or directory`。
#   缺 ② 的现象：编译能过，但 RC 规则退化成 `cl.exe rc.exe ...` → RC1107。
#   （仅把 VS 的 cmake 提前不够——端口构建阶段 cmake --build 仍会选中
#    MSYS2 那个，必须整个换掉 PATH。）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\vcpkg-install.ps1 drogon:x64-windows
#   powershell -ExecutionPolicy Bypass -File tools\vcpkg-install.ps1          # 按 manifest 装

param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Packages
)

$VS = 'D:\Program Files\Microsoft Visual Studio\18\Community'
$WINDDK = 'D:\Windows Kits\10'

# MSVC 与 SDK 版本按「最新」取，与本机 tools/build-msvc.bat 同口径。
# 注意：这里必须写在一行内——Windows PowerShell 5.1 不把行尾的 `|`
# 当作续行符，拆两行会报「意外的标记")"」。
$MSVCVER = (Get-ChildItem -Path (Join-Path $VS 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$SDKVER = (Get-ChildItem -Path (Join-Path $WINDDK 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name

# 版本探测失败就明确退出，别带着空路径往下跑：空 $MSVCVER 会让
# PATH/INCLUDE/LIB 全部指向不存在的目录，症状是端口在编译器自检时
# 报一串看不懂的错，而真正的原因在这里（2026-10-15 实测踩过）。
if ([string]::IsNullOrEmpty($MSVCVER)) { Write-Error "探测 MSVC 工具集版本失败: $(Join-Path $VS 'VC\Tools\MSVC')"; exit 1 }
if ([string]::IsNullOrEmpty($SDKVER)) { Write-Error "探测 Windows SDK 版本失败: $(Join-Path $WINDDK 'Include')"; exit 1 }

$VCTOOLS = Join-Path $VS "VC\Tools\MSVC\$MSVCVER"
$VSCMAKE = Join-Path $VS 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$VSNINJA = Join-Path $VS 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'

$env:PATH = @(
    (Join-Path $VCTOOLS 'bin\Hostx64\x64'),
    (Join-Path $WINDDK "bin\$SDKVER\x64"),
    $VSCMAKE,
    $VSNINJA,
    'C:\Windows\System32',
    'C:\Windows'
) -join ';'
$env:INCLUDE = "$VCTOOLS\include;$WINDDK\Include\$SDKVER\ucrt;$WINDDK\Include\$SDKVER\um;$WINDDK\Include\$SDKVER\shared"
$env:LIB = "$VCTOOLS\lib\x64;$WINDDK\Lib\$SDKVER\ucrt\x64;$WINDDK\Lib\$SDKVER\um\x64"
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:VCPKG_VISUAL_STUDIO_PATH = $VS
$env:VCPKG_KEEP_ENV_VARS = 'PATH;INCLUDE;LIB;VCPKG_VISUAL_STUDIO_PATH'
# 单并发：端口默认 -jN，多个 cl 同时 /FS 写同一个 PDB 会互相锁死，
# 表现为某个 cl.exe 挂住十几分钟、CPU 为 0（2026-10-15 实测 c-ares 两次）。
$env:VCPKG_MAX_CONCURRENCY = '1'
$env:VCPKG_CONCURRENCY = '1'

# 只编 Release 的 overlay triplet：debug 配置本项目用不上，
# 单并发下编两份要半小时以上。见 tools/vcpkg-triplets/ 里的说明。
# server 裁撤后 tools/vcpkg-ports（trantor 专用）已删除，overlay 只剩 triplets。
#
# 两条 flag 都内联写死，不经过变量：2026-10-15 实测变量版在这台机器的
# 后台任务里 $OverlayTriplets 解析成空串（同一条赋值语句的 $OverlayPorts
# 却正常，原因始终没查出来），vcpkg 收到 --overlay-triplets= 就静默
# 用回自带 triplet，debug 配置照编。内联版没有这一层可出岔子的地方。

Write-Host "MSVC : $VCTOOLS"
Write-Host "SDK  : $WINDDK\$SDKVER"

& D:\vcpkg\vcpkg.exe install @Packages --overlay-triplets=D:\cd\tree\genealogy\tools\vcpkg-triplets --overlay-ports=D:\cd\tree\genealogy\tools\vcpkg-ports
exit $LASTEXITCODE
