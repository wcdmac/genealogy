cmake_minimum_required(VERSION 3.21)

project(genealogy
        VERSION 0.1.0
        DESCRIPTION "跨平台家谱管理软件：核心 C++20，GUI 禁 Qt，依赖仅限宽松许可"
        LANGUAGES CXX C)

# ---------- 全局约定 ----------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 默认按系统 ANSI 代码页（中文 Windows 为 936/GBK）读源码，
# 而本仓源码为 UTF-8（含中文注释与中文字符串字面量），错位解码会
# 产生游离引号/花括号导致连环语法错误（C4819/C2001/C2143）。
# /utf-8 同时统一源字符集与执行字符集：窄字符串字面量以 UTF-8
# 入二进制，与 SQLite 的 UTF-8 存储一致。
if(MSVC)
  add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------- 构建开关 ----------
option(BUILD_CORE   "构建 libgenealogy-core 核心库" ON)
option(BUILD_CLI    "构建 genea 命令行" ON)
option(BUILD_TESTS  "构建单元测试（Catch2）" ON)
option(BUILD_SERVER "构建 REST 服务（Drogon，MIT）" OFF)
option(BUILD_GUI    "构建桌面 GUI（Dear ImGui + GLFW，MIT/zlib）" OFF)

# ---------- vcpkg 工具链 ----------
# 依赖白名单见 shared/licenses/allowlist.txt，CI 有 license_check.py 卡点。
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
      CACHE FILEPATH "vcpkg toolchain file")
endif()

# ---------- 依赖 ----------
# nlohmann-json / cxxopts / catch2 走 vcpkg manifest（见 vcpkg.json）。
# SQLite 用官方 amalgamation 自建静态库（third_party/sqlite3），
# 原因见该目录 CMakeLists.txt 头部注释（vcpkg sqlite3 端口的 RC1107 问题）。
find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(third_party/sqlite3)

if(BUILD_CLI)
  find_package(cxxopts CONFIG REQUIRED)
endif()

if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
endif()

if(BUILD_SERVER)
  find_package(Drogon CONFIG REQUIRED)
endif()

enable_testing()

if(BUILD_CORE)
  add_subdirectory(core)
endif()

if(BUILD_CLI)
  add_subdirectory(cli)
endif()

if(BUILD_SERVER)
  add_subdirectory(server)
endif()

if(BUILD_GUI)
  add_subdirectory(gui)
endif()
