# libgenealogy-core：领域核心（无 UI、无网络依赖）
#
# 源文件分两组：
#   1. 手写源码（src/）
#   2. 生成源码（embedded_schema.cpp）：shared/schema/*.sql 在构建时
#      由 cmake/EmbedSql.cmake 嵌入为 C++ 字符串，core 不依赖运行时文件路径。

set(SCHEMA_DIR ${PROJECT_SOURCE_DIR}/shared/schema)
set(EMBED_SCRIPT ${PROJECT_SOURCE_DIR}/cmake/EmbedSql.cmake)
set(EMBED_TEXT_SCRIPT ${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
set(EMBED_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_schema.cpp)
set(RULES_DIR ${PROJECT_SOURCE_DIR}/shared/rules)
set(RULES_VALID_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/rules_validation.cpp)
set(RULES_MOURNING_OUT
    ${CMAKE_CURRENT_BINARY_DIR}/generated/rules_mourning_ming_qing.cpp)
set(RULES_APPEL_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/rules_appellation.cpp)

file(GLOB SCHEMA_FILES CONFIGURE_DEPENDS "${SCHEMA_DIR}/*.sql")

# 规则表数据（shared/rules/*.json）嵌入二进制：算法零文件依赖，
# 规则更新走数据替换 + 重新构建（ADR-6 规则外置）。
add_custom_command(
  OUTPUT ${RULES_MOURNING_OUT}
  COMMAND ${CMAKE_COMMAND}
          -DIN=${RULES_DIR}/mourning_ming_qing.json
          -DOUT=${RULES_MOURNING_OUT}
          -DSYMBOL=kMourningMingQingJson
          -DNAMESPACE=genealogy::lineage::rules_data
          -DHEADER=genealogy/lineage/rules_data.hpp
          -P ${EMBED_TEXT_SCRIPT}
  DEPENDS ${RULES_DIR}/mourning_ming_qing.json ${EMBED_TEXT_SCRIPT}
  COMMENT "嵌入五服规则表（明清定型版）"
  VERBATIM)

add_custom_command(
  OUTPUT ${RULES_APPEL_OUT}
  COMMAND ${CMAKE_COMMAND}
          -DIN=${RULES_DIR}/appellation.json
          -DOUT=${RULES_APPEL_OUT}
          -DSYMBOL=kAppellationJson
          -DNAMESPACE=genealogy::lineage::rules_data
          -DHEADER=genealogy/lineage/rules_data.hpp
          -P ${EMBED_TEXT_SCRIPT}
  DEPENDS ${RULES_DIR}/appellation.json ${EMBED_TEXT_SCRIPT}
  COMMENT "嵌入称谓规则表"
  VERBATIM)

add_custom_command(
  OUTPUT ${RULES_VALID_OUT}
  COMMAND ${CMAKE_COMMAND}
          -DIN=${RULES_DIR}/validation.json
          -DOUT=${RULES_VALID_OUT}
          -DSYMBOL=kValidationJson
          -DNAMESPACE=genealogy::validation::rules_data
          -DHEADER=genealogy/validation/rules_data.hpp
          -P ${EMBED_TEXT_SCRIPT}
  DEPENDS ${RULES_DIR}/validation.json ${EMBED_TEXT_SCRIPT}
  COMMENT "嵌入一致性校验规则表"
  VERBATIM)

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
    src/model/lineage.cpp
    src/model/branch.cpp
    src/model/place.cpp
    src/model/union.cpp
    src/model/event.cpp
    src/repository/sqlite_util.cpp
    src/repository/transaction.cpp
    src/repository/database.cpp
    src/repository/person_repository.cpp
    src/repository/bootstrap.cpp
    src/repository/lineage_repository.cpp
    src/repository/branch_repository.cpp
    src/repository/place_repository.cpp
    src/repository/union_repository.cpp
    src/repository/event_repository.cpp
    src/service/person_service.cpp
    src/service/lineage_service.cpp
    src/service/branch_service.cpp
    src/service/place_service.cpp
    src/service/union_service.cpp
    src/service/event_service.cpp
    src/lineage/graph.cpp
    src/lineage/relationship.cpp
    src/lineage/generation.cpp
    src/lineage/mourning.cpp
    src/lineage/appellation.cpp
    src/lineage/rules.cpp
  src/calendar/chinese_calendar.cpp
  src/calendar/calendar_tables.cpp
  src/calendar/era.cpp
  src/calendar/era_tables.cpp
  src/io/gedcom.cpp
  src/io/synthetic.cpp
  src/lineage/tree.cpp
  src/validate/validator.cpp
  src/repository/bulk_load.cpp
    ${EMBED_OUT}
    ${RULES_MOURNING_OUT}
    ${RULES_APPEL_OUT}
    ${RULES_VALID_OUT}
)

target_include_directories(genealogy-core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
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
