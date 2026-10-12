# libgenealogy-core：领域核心（无 UI、无网络依赖）
#
# 源文件分两组：
#   1. 手写源码（src/）
#   2. 生成源码（embedded_schema.cpp）：shared/schema/*.sql 在构建时
#      由 cmake/EmbedSql.cmake 嵌入为 C++ 字符串，core 不依赖运行时文件路径。

set(SCHEMA_DIR ${PROJECT_SOURCE_DIR}/shared/schema)
set(EMBED_SCRIPT ${PROJECT_SOURCE_DIR}/cmake/EmbedSql.cmake)
set(EMBED_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_schema.cpp)

file(GLOB SCHEMA_FILES CONFIGURE_DEPENDS "${SCHEMA_DIR}/*.sql")

add_custom_command(
  OUTPUT ${EMBED_OUT}
  COMMAND ${CMAKE_COMMAND}
          -DSCHEMA_DIR=${SCHEMA_DIR}
          -DOUT=${EMBED_OUT}
          -P ${EMBED_SCRIPT}
  DEPENDS ${SCHEMA_FILES} ${EMBED_SCRIPT}
  COMMENT "嵌入 SQL schema 到 embedded_schema.cpp"
  VERBATIM)

add_library(genealogy-core STATIC)
target_sources(genealogy-core
  PRIVATE
    src/ulid.cpp
    src/util/time.cpp
    src/model/genealogy_date.cpp
    src/model/person.cpp
    src/repository/database.cpp
    src/repository/person_repository.cpp
    src/repository/bootstrap.cpp
    src/service/person_service.cpp
    ${EMBED_OUT}
)

target_include_directories(genealogy-core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)

# 注意：spdlog/fmt 暂不引入——M0 核心零日志调用，且本机 VS2026 + SDK 26100
# 的 rc.exe 与 vcpkg 的 spdlog 端口有兼容问题（RC1107）。M1 落地日志时再接，
# 届时改用 --static 三元组或升级 vcpkg 端口验证。
target_link_libraries(genealogy-core
  PRIVATE
    nlohmann_json::nlohmann_json
    genealogy-sqlite3
)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
