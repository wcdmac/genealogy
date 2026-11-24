# SQLite：官方 amalgamation 源码已 vendor 到本目录，编译为静态库。
#
# 为什么不走 vcpkg 的 sqlite3 端口：本机 VS2026（18）+ 中文语言包 +
# CMake Ninja 组合下，带 .rc 的端口构建会触发已知的 RC1107 问题
# （cl 的中文本地化输出污染 CMake 生成的 RC 规则），spdlog/sqlite3 端口
# 先后中招。amalgamation 是 SQLite 官方推荐的分发形态（公有领域），
# 直接编译反而少一层端口构建风险。FTS5 / JSON1 在此显式启用。
#
# 包含路径说明：只把 sqlite3.h / sqlite3ext.h 复制到干净的 include 目录
# 再挂路径，不把整个源码目录挂上去——vendor 目录若有 extensionless 的
# version 文件会遮蔽标准库 <version> 头，导致 nlohmann/json 编译报语法错误。

# SQLite amalgamation 官方分发形态已 vendor 到本目录（sqlite3.c/.h/
# sqlite3ext.h，公有领域）。configure 期不联网——离线/内网/CI 网络抖动
# 下 FetchContent 会让整个 configure 挂死，与本项目"无联网组件"定位冲突。
set(SQLITE3_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(SQLITE3_PUBLIC_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${SQLITE3_PUBLIC_INCLUDE})
configure_file(${SQLITE3_SOURCE_DIR}/sqlite3.h
               ${SQLITE3_PUBLIC_INCLUDE}/sqlite3.h COPYONLY)
configure_file(${SQLITE3_SOURCE_DIR}/sqlite3ext.h
               ${SQLITE3_PUBLIC_INCLUDE}/sqlite3ext.h COPYONLY)

add_library(genealogy-sqlite3 STATIC ${SQLITE3_SOURCE_DIR}/sqlite3.c)

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
