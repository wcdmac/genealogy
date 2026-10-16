@echo off
chcp 65001 >nul
REM ============================================================
REM genealogy local build helper WITH REST server (Windows / MSVC)
REM
REM Same environment assembly as build-msvc.bat (see that file's
REM header for why vcvars64.bat cannot be used here). The two
REM differences:
REM   1. -DBUILD_SERVER=ON so add_subdirectory(server) runs;
REM   2. -DVCPKG_MANIFEST_FEATURES=server so manifest mode pulls
REM      Drogon (the "server" feature in vcpkg.json). Without it
REM      the configure step installs only the default deps and
REM      find_package(Drogon) fails.
REM
REM First-time setup: Drogon and its deps must be installed into
REM the vcpkg tree. On this machine that cannot go through plain
REM `vcpkg install` -- see tools/vcpkg-install.ps1 (reg.exe
REM blacklist + MSYS2 32-bit cmake + single-concurrency PDB
REM deadlock, all worked around there).
REM
REM Usage: cmd /c tools\build-server-msvc.bat
REM ============================================================

setlocal enabledelayedexpansion

set "VS=D:\Program Files\Microsoft Visual Studio\18\Community"
set "VCTOOLSROOT=%VS%\VC\Tools\MSVC"
if not exist "%VCTOOLSROOT%" (
  echo [ERROR] MSVC tools not found: %VCTOOLSROOT%
  exit /b 1
)

set "MSVCVER="
pushd "%VCTOOLSROOT%"
for /f "delims=" %%v in ('dir /b /ad /o-n') do if not defined MSVCVER set "MSVCVER=%%v"
popd
if not defined MSVCVER (
  echo [ERROR] no MSVC toolset version found
  exit /b 1
)

set "WINDDK=D:\Windows Kits\10"
if not exist "%WINDDK%\Include" set "WINDDK=%PROGRAMFILES(X86)%\Windows Kits\10"
dir /b "%WINDDK%\Include" >nul 2>&1 || (
  echo [ERROR] Windows SDK Include directory not found
  exit /b 1
)

set "SDKVER="
pushd "%WINDDK%\Include"
for /f "delims=" %%s in ('dir /b /ad /o-n') do if not defined SDKVER set "SDKVER=%%s"
popd
if not defined SDKVER (
  echo [ERROR] no Windows SDK version found
  exit /b 1
)

echo [INFO] MSVC %MSVCVER%  Windows SDK %SDKVER%

set "VCTOOLS=%VCTOOLSROOT%\%MSVCVER%"
set "VCBIN=%VCTOOLS%\bin\Hostx64\x64"
set "SDKBIN=%WINDDK%\bin\%SDKVER%\x64"
set "SDKINC=%WINDDK%\Include\%SDKVER%"
set "SDKLIB=%WINDDK%\Lib\%SDKVER%"

if not exist "%VCBIN%\cl.exe" (
  echo [ERROR] cl.exe missing under the MSVC toolset bin dir
  exit /b 1
)
dir /b "%SDKBIN%\rc.exe" >nul 2>&1 || (
  echo [ERROR] rc.exe missing under the Windows SDK bin dir
  exit /b 1
)

set "PATH=%VCBIN%;%SDKBIN%;%PATH%"
set "INCLUDE=%VCTOOLS%\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared"
set "LIB=%VCTOOLS%\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=D:\vcpkg"
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo [ERROR] vcpkg toolchain not found
  exit /b 1
)

set "SRC=%~dp0.."
set "BUILD=%SRC%\build"

REM overlay 必须经环境变量传给 manifest 模式：CMake 的 vcpkg 工具链会在
REM 配置期自己跑一次 vcpkg install（manifest 模式），那次子调用看不到
REM 命令行的 --overlay-*，只能读环境变量。少了这一步，manifest 安装会按
REM 自带 triplet 重算 ABI → 重编 drogon → /MP 死锁再现。
set "VCPKG_OVERLAY_TRIPLETS=%SRC%\tools\vcpkg-triplets"
set "VCPKG_OVERLAY_PORTS=%SRC%\tools\vcpkg-ports"

echo [INFO] Configure (BUILD_SERVER=ON, manifest feature server)...
cmake -S "%SRC%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DBUILD_SERVER=ON ^
  -DVCPKG_MANIFEST_FEATURES=server || exit /b 1

echo [INFO] Build...
cmake --build "%BUILD%" --parallel || exit /b 1

echo [INFO] Test...
ctest --test-dir "%BUILD%" --output-on-failure || exit /b 1

echo BUILD_SERVER_PIPELINE_OK
exit /b 0
