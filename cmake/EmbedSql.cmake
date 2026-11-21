# 将 shared/schema/*.sql 嵌入为 C++ 源文件（原始字符串字面量）。
# 用法：cmake -DSCHEMA_DIR=<dir> -DOUT=<file.cpp> -P EmbedSql.cmake
#
# 注意：必须用 string(APPEND) 逐段拼接。不要写 set(VAR "a" "b") 多参数
# 形式——CMake 会把它组成分号分隔列表，file(WRITE) 会把分号写进产物，
# 导致生成的 .cpp 行首出现 ";" 从而触发 MSVC C2014。

foreach(_var SCHEMA_DIR OUT)
  if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
    message(FATAL_ERROR "EmbedSql.cmake 必须提供 -D${_var}=<path>")
  endif()
endforeach()

file(GLOB SCHEMA_FILES "${SCHEMA_DIR}/*.sql")
list(SORT SCHEMA_FILES)

set(content "")
string(APPEND content "// 由 cmake/EmbedSql.cmake 自动生成，请勿手改\n")
string(APPEND content "#include <genealogy/repository/embedded_schema.hpp>\n\n")
string(APPEND content "namespace genealogy::embedded_schema {\n\n")

set(entries "")
foreach(f ${SCHEMA_FILES})
  get_filename_component(name "${f}" NAME_WE)
  file(READ "${f}" sql)
  string(APPEND content
         "static const char* kSql_${name} = R\"GENSQL(${sql})GENSQL\";\n\n")
  string(APPEND entries "  Migration{\"${name}\", kSql_${name}},\n")
endforeach()

string(APPEND content "static const Migration kMigrations[] = {\n${entries}};\n\n")
string(APPEND content
       "const Migration* migrations() noexcept { return kMigrations; }\n")
string(APPEND content "std::size_t migration_count() noexcept {\n")
string(APPEND content "  return sizeof(kMigrations) / sizeof(kMigrations[0]);\n")
string(APPEND content "}\n\n")
string(APPEND content "}  // namespace genealogy::embedded_schema\n")

file(WRITE "${OUT}" "${content}")
message(STATUS "EmbedSql: ${SCHEMA_FILES} -> ${OUT}")
