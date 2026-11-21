# genea 命令行（db / person / lineage / branch / place / union / event / date / io）

add_executable(genea
  src/main.cpp
  src/cmd_entity.cpp
  src/cmd_poem.cpp
  src/cmd_io.cpp
  src/cmd_backup.cpp
  src/cmd_migration.cpp
  src/cmd_source.cpp
  src/cmd_document.cpp
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

# CLI 门禁：把 main.cpp 编进测试二进制，任何翻译单元的编译错误
# （如字符串内嵌换行、未声明变量）都会让构建失败；同时覆盖命令分派。
if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  add_executable(genealogy-cli-tests
    tests/test_cli.cpp
    src/main.cpp
    src/cmd_entity.cpp
    src/cmd_poem.cpp
    src/cmd_io.cpp
    src/cmd_backup.cpp
    src/cmd_migration.cpp
    src/cmd_source.cpp
    src/cmd_document.cpp
  )
  target_include_directories(genealogy-cli-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_link_libraries(genealogy-cli-tests
    PRIVATE
      genealogy-core
      genealogy-sqlite3
      cxxopts::cxxopts
      Catch2::Catch2WithMain
  )
  target_compile_features(genealogy-cli-tests PRIVATE cxx_std_20)
  # main.cpp 的 wmain/main 由该宏排除，避免与 Catch2WithMain 冲突；
  # run() 仍被编入，编译错误照常拦截。
  target_compile_definitions(genealogy-cli-tests PRIVATE GENEA_NO_MAIN)
  add_test(NAME genealogy-cli-tests COMMAND genealogy-cli-tests)
endif()

install(TARGETS genea RUNTIME DESTINATION bin)
