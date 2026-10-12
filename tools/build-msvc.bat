@echo off
chcp 65001 >nul
REM ============================================================
REM genealogy local build helper (Windows / MSVC)
REM
REM Why this script exists:
REM  1. This machine has MSYS2 g++ (32-bit) on PATH, so CMake picks
REM     GNU g++ instead of MSVC by default, which breaks 64-bit
REM     vcpkg package config checks (e.g. Catch2 version mismatch).
REM  2. vcvars64.bat cannot be used here: it calls reg.exe to locate
REM     the Windows SDK, and reg.exe is on this machine's WorkBuddy
REM     Program Blacklist (Security Center - Command Security), so
REM     the SDK bin dir never reaches PATH and rc.exe/mt.exe go
REM     missing (manifest embedding then fails).
REM
REM This script assembles the MSVC x64 environment manually:
REM latest MSVC toolset + latest Windows SDK, no registry access.
REM
REM cmd.exe quirks this script works around (all verified on this
REM machine 2026-10-12):
REM  - .bat files must use CRLF line endings (LF-only breaks parsing);
REM  - parenthesized blocks ( "|| (" / "if ... (" ) are parsed AFTER
REM    percent expansion, so a block whose text expands to contain
REM    "(" (e.g. "C:\Program Files (x86)\...") mis-parses with
REM    "\Windows was unexpected at this time". Rule: never put a
REM    paren-containing variable inside a block; echo generic text;
REM  - this machine's Windows SDK lives at D:\Windows Kits\10, not
REM    under C:\Program Files (x86); try D: first, fall back to
REM    %PROGRAMFILES(X86)% (single-line set, no block).
REM
REM Usage: cmd /c tools\build-msvc.bat
REM ============================================================

setlocal enabledelayedexpansion

REM --- locate latest MSVC toolset version ---
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

REM --- locate latest Windows SDK version (D: first, then x86 fallback) ---
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

REM --- assemble environment (equivalent of vcvars64, minus reg.exe) ---
set "PATH=%VCBIN%;%SDKBIN%;%PATH%"
set "INCLUDE=%VCTOOLS%\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared"
set "LIB=%VCTOOLS%\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"

REM --- vcpkg root (adjust to the actual location on this machine) ---
if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=D:\vcpkg"
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo [ERROR] vcpkg toolchain not found
  exit /b 1
)

set "SRC=%~dp0.."
set "BUILD=%SRC%\build"

echo [INFO] Configure...
cmake -S "%SRC%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl || exit /b 1

echo [INFO] Build...
cmake --build "%BUILD%" --parallel || exit /b 1

echo [INFO] Test...
ctest --test-dir "%BUILD%" --output-on-failure || exit /b 1

echo BUILD_PIPELINE_OK
exit /b 0
