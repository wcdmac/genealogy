# genea 命令行（db / person / lineage / branch / place / union / event / date / io）

add_executable(genea
  src/main.cpp
  src/cmd_entity.cpp
  src/cmd_poem.cpp
  src/cmd_io.cpp
  src/cmd_backup.cpp
)

target_include_directories(genea
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(genea
  PRIVATE
    genealogy-core
    cxxopts::cxxopts
    # 备份命令直接用 sqlite3 C API（VACUUM INTO）；genealogy-sqlite3
    # 是 third_party 自建的静态库，PRIVATE 链入顺带拿到 sqlite3.h
    genealogy-sqlite3
)

set_target_properties(genea PROPERTIES OUTPUT_NAME genea)

install(TARGETS genea RUNTIME DESTINATION bin)
