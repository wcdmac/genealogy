# genea 命令行（M0：db / person）

add_executable(genea
  src/main.cpp
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
