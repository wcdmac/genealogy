# SQLite：官方 amalgamation 经 FetchContent 引入，编译为静态库。
#
# 为什么不走 vcpkg 的 sqlite3 端口：本机 VS2026（18）+ 中文语言包 +
# CMake Ninja 组合下，带 .rc 的端口构建会触发已知的 RC1107 问题
# （cl 的中文本地化输出污染 CMake 生成的 RC 规则），spdlog/sqlite3 端口
# 先后中招。amalgamation 是 SQLite 官方推荐的分发形态（公有领域），
# 直接编译反而少一层端口构建风险。FTS5 / JSON1 在此显式启用。
#
# 包含路径说明：只把 sqlite3.h / sqlite3ext.h 复制到干净的 include 目录
# 再挂路径。不能把整个源码目录挂上去—— amalgamation 解包目录里有个无
# 扩展名的 version 文件（内容就是版本号），会遮蔽标准库 <version> 头，
# 导致 nlohmann/json 包含链上的编译全面报语法错误。

include(FetchContent)

FetchContent_Declare(
  sqlite3
  URL https://sqlite.org/2026/sqlite-autoconf-3530400.tar.gz
  URL_HASH SHA512=c24374e9393a943157f533f96e89e6c5743e5f5aad169d8393cff3088ca5ccbe5cc0561681ace49c349d0fe402298ee2624d319f422967247ce0792e3b3aa01e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(sqlite3)

set(SQLITE3_PUBLIC_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${SQLITE3_PUBLIC_INCLUDE})
configure_file(${sqlite3_SOURCE_DIR}/sqlite3.h
               ${SQLITE3_PUBLIC_INCLUDE}/sqlite3.h COPYONLY)
configure_file(${sqlite3_SOURCE_DIR}/sqlite3ext.h
               ${SQLITE3_PUBLIC_INCLUDE}/sqlite3ext.h COPYONLY)

add_library(genealogy-sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)

target_include_directories(genealogy-sqlite3 PUBLIC ${SQLITE3_PUBLIC_INCLUDE})

target_compile_definitions(genealogy-sqlite3
  PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_DQS=0
)

if(MSVC)
  target_compile_options(genealogy-sqlite3 PRIVATE /W0)
else()
  target_compile_options(genealogy-sqlite3 PRIVATE -w)
endif()
