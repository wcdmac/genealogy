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
# 定位（2026-11-21 收口）：单机桌面软件——CLI + 本地 GUI，无任何联网组件。
# 原 REST 服务与 Web 参考客户端随「单机定位」整体裁撤（git 历史可考）；
# 家谱按「单谱单库」运营：一个数据库文件只承载一部家谱。
option(BUILD_CORE   "构建 libgenealogy-core 核心库" ON)
option(BUILD_CLI    "构建 genea 命令行" ON)
option(BUILD_TESTS  "构建单元测试（Catch2）" ON)
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

enable_testing()

if(BUILD_CORE)
  add_subdirectory(core)
endif()

if(BUILD_CLI)
  add_subdirectory(cli)
endif()

if(BUILD_GUI)
  add_subdirectory(gui)
endif()

# ---------- 打包（M6-1：三平台产物） ----------
# 生成器按平台选：Windows ZIP / macOS DRAGNDROP(dmg) / Linux TGZ。
# 都不引外部打包工具（NSIS 要在 CI 装、hdiutil 只有 macOS 有），
# CPack 内置支持，本地 cpack 一条命令出包。
set(CPACK_PACKAGE_NAME "genealogy")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "wcdmac")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wcdmac/genealogy")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
if(APPLE)
  # CMake 4.x 的 DMG 生成器移除了 license-SLA 的隐式支持：设了
  # CPACK_RESOURCE_FILE_LICENSE 而不显式声明 SLA 行为会直接报错
  # （2026-11-14 CI 六跑 macOS DRAGNDROP 首挂）。本项目不做 SLA 弹窗，
  # 显式关闭。
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
endif()
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  # 生成器名大小写敏感：正确名是 DragNDrop。M6-1 起写的全大写
  # DRAGNDROP 从未真正打包成功过，直到 CI 七跑在真 macOS 上
  # 报「Could not create CPack generator: DRAGNDROP」才揭发。
  set(CPACK_GENERATOR "DragNDrop")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_SOURCE_IGNORE_FILES "/\\\\.git/;/build/")
include(CPack)

# Windows 运行时 DLL 统一收进包的 bin/。server/gui 的 POST_BUILD 已把
# vcpkg DLL 全量拷到 build/bin，这里 install 期 glob 收集。
# 两个前车之鉴：
# ① install(CODE [[ ... ]]) —— bracket 参数不展开 ${CMAKE_BINARY_DIR}，
#    glob 到字面路径，静悄悄地装出没有 DLL 的包；必须用普通字符串 +
#    转义 install 期变量（\${...}）。
# ② $<TARGET_RUNTIME_DLLS> genex —— 只解析到直接传递依赖（drogon/
#    trantor/cares/glfw3），drogon 内部的 jsoncpp/zlib/brotli/openssl
#    不在其依赖树里，会漏。POST_BUILD 的 glob 拷贝才是全量。
if(WIN32)
  install(CODE "
    file(GLOB _runtime_dlls \"${CMAKE_BINARY_DIR}/bin/*.dll\")
    if(_runtime_dlls)
      file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\"
           TYPE SHARED_LIBRARY FILES \${_runtime_dlls})
    endif()
  ")
endif()
