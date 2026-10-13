# genea 命令行（db / person / lineage / branch / place / union / event / date / io）

add_executable(genea
  src/main.cpp
  src/cmd_entity.cpp
  src/cmd_io.cpp
)

target_include_directories(genea
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(genea
  PRIVATE
    genealogy-core
    cxxopts::cxxopts
)

set_target_properties(genea PROPERTIES OUTPUT_NAME genea)

install(TARGETS genea RUNTIME DESTINATION bin)
