# 将任意文本文件嵌入为 C++ 源文件（原始字符串字面量）。
# 用法：cmake -DIN=<file> -DOUT=<file.cpp> -DSYMBOL=<name>
#       -DNAMESPACE=<ns> -DHEADER=<header.hpp> -P EmbedText.cmake
#
# 注意：必须用 string(APPEND) 逐段拼接。不要写 set(VAR "a" "b") 多参数
# 形式——CMake 会把它组成分号分隔列表，file(WRITE) 会把分号写进产物，
# 导致生成的 .cpp 行首出现 ";" 从而触发 MSVC C2014。

if(NOT DEFINED IN OR NOT DEFINED OUT OR NOT DEFINED SYMBOL)
  message(FATAL_ERROR "EmbedText: 需要 IN / OUT / SYMBOL")
endif()

set(ns "")
if(DEFINED NAMESPACE)
  set(ns "${NAMESPACE}")
endif()

set(hdr "")
if(DEFINED HEADER)
  set(hdr "${HEADER}")
endif()

file(READ "${IN}" text)

set(content "")
string(APPEND content "// 由 cmake/EmbedText.cmake 自动生成，请勿手改\n")
if(hdr)
  string(APPEND content "#include <${hdr}>\n\n")
endif()
if(ns)
  string(APPEND content "namespace ${ns} {\n\n")
endif()
string(APPEND content
       "const char* ${SYMBOL} = R\"GENTEXT(${text})GENTEXT\";\n")
if(ns)
  string(APPEND content "\n}  // namespace ${ns}\n")
endif()

file(WRITE "${OUT}" "${content}")
message(STATUS "EmbedText: ${IN} -> ${OUT}")
