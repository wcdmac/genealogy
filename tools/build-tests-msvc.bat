@echo off
chcp 65001 >nul
REM ============================================================
REM genealogy targeted build helper (Windows / MSVC)
REM
REM Same MSVC environment assembly as tools/build-msvc.bat (see the
REM rationale there), and builds only the test suites and the CLI.
REM
REM Usage: cmd /c tools\build-tests-msvc.bat [extra cmake --build args...]
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

set "PATH=%VCBIN%;%SDKBIN%;%PATH%"
set "INCLUDE=%VCTOOLS%\include;%SDKINC%\ucrt;%SDKINC%\um;%SDKINC%\shared"
set "LIB=%VCTOOLS%\lib\x64;%SDKLIB%\ucrt\x64;%SDKLIB%\um\x64"

set "SRC=%~dp0.."
set "BUILD=%SRC%\build"

echo [INFO] Build (core-tests, gui-tests, CLI)...
cmake --build "%BUILD%" --target genealogy-core-tests genealogy-gui-tests genea --parallel %1 %2 || exit /b 1
echo [OK]
