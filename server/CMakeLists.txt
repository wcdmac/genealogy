# M2：REST 服务（Drogon，MIT）。
#
# 启用：cmake -S . -B build -DBUILD_SERVER=ON -DVCPKG_MANIFEST_FEATURES=server
#   （Drogon 经 vcpkg manifest 的 "server" feature 提供；本机首次安装见
#     tools/vcpkg-install.ps1——直接 vcpkg install 会被 reg.exe 黑名单、
#     MSYS2 32 位 cmake、/MP 并发死锁三件事坑住）
#   overlay（triplet + trantor 去 openssl）必须经 VCPKG_OVERLAY_TRIPLETS /
#   VCPKG_OVERLAY_PORTS 环境变量传入：manifest 模式配置期会自己跑一次
#   vcpkg install，那次子调用看不到命令行参数。
#
# 分文件按资源域拆：routes_persons（人物 + 世系算法）、
# routes_entities（基础资源 CRUD）、routes_misc（检索/统计/校验/导入导出/运维）。
# 路由函数一律走 libgenealogy-core 的用例层，不另写旁路（设计文档 ADR-1）。
#
# 拆成 库 + 瘦 main 的理由：HTTP 集成测试要链路由函数，而 CMake 不允许把
# 可执行文件链进别的目标。早先的写法是测试目标直接链 genealogy-server，
# 配置期就报 "Target of type EXECUTABLE may not be linked into another target"。

add_library(genealogy-server-lib STATIC
  src/auth.cpp
  src/routes_auth.cpp
  src/runtime.cpp
  src/problem.cpp
  src/dto.cpp
  src/cors.cpp
  src/body_guard.cpp
  src/event_bus.cpp
  src/routes_stream.cpp
  src/routes_persons.cpp
  src/routes_entities.cpp
  src/routes_misc.cpp
  src/routes_sources.cpp
  src/routes_documents.cpp
  src/audit.cpp
  src/idempotency.cpp
  src/rate_limit.cpp
)

target_include_directories(genealogy-server-lib
  PUBLIC
    ${PROJECT_SOURCE_DIR}/server/include
)

# PUBLIC：静态库的包含目录与传递依赖要带给使用方（main 与测试）
target_link_libraries(genealogy-server-lib
  PUBLIC
    genealogy-core
    genealogy-sqlite3
    Drogon::Drogon
)

add_executable(genealogy-server
  src/main.cpp
)

target_link_libraries(genealogy-server
  PRIVATE
    genealogy-server-lib
)

# openapi.yaml 是客户端代码生成的唯一事实源，服务端原样托管。
# 拷到可执行文件旁边，/openapi.yaml 就近读取（见 routes_misc.cpp 的查找顺序）。
add_custom_command(TARGET genealogy-server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${PROJECT_SOURCE_DIR}/shared/openapi/openapi.yaml
          $<TARGET_FILE_DIR:genealogy-server>/openapi.yaml
  COMMENT "拷贝 openapi.yaml 到 genealogy-server 旁边"
)

# 分发：exe 进 bin/，openapi.yaml 跟 exe 同目录（服务的查找顺序第一站）。
# OPTIONAL：install 期该文件必在（POST_BUILD 已拷），留着是防 strip 场景。
# Windows 运行时 DLL 由根 CMakeLists 的 install(CODE) 从 build/bin 全量收拢
# （TARGET_RUNTIME_DLLS genex 会漏 drogon 内部的 jsoncpp/zlib/brotli/openssl）。
install(TARGETS genealogy-server RUNTIME DESTINATION bin)
install(FILES "${CMAKE_BINARY_DIR}/bin/openapi.yaml" DESTINATION bin OPTIONAL)

# ---------------------------------------------------------------- 测试
# HTTP 集成测试：起真实 Drogon 服务打真实 HTTP（见 tests/test_http.cpp 头部说明）
if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  add_executable(genealogy-server-tests
    tests/test_http.cpp
  )
  target_include_directories(genealogy-server-tests
    PRIVATE ${PROJECT_SOURCE_DIR}/server/include
  )
  target_link_libraries(genealogy-server-tests
    PRIVATE
      genealogy-server-lib
      Catch2::Catch2WithMain
  )
  add_test(NAME genealogy-server-tests COMMAND genealogy-server-tests)
endif()

# ---------------------------------------------------------------- DLL 分发
# Drogon 及其传递依赖（trantor/jsoncpp/zlib/c-ares）在 Windows 上都是 DLL。
# 链接期只用导入库，运行期加载器按「exe 所在目录 → PATH」的顺序找 DLL——
# ctest 是直接起 exe 的，调用方的 PATH 里未必有 vcpkg 的 bin 目录，
# 于是出现「手工跑得过、ctest 加载器找不到 DLL」的环境相关失败。
# 拷到 exe 旁边一劳永逸：Windows 加载器第一站就是 exe 目录，
# 顺带也让 build/bin 成为自包含的分发目录。
# （Linux/macOS 的 rpath 由 CMake 自动处理，无需此步。）
# 放在文件末尾：add_custom_command(TARGET ...) 要求目标已定义，
# 而 genealogy-server-tests 在下面的 if(BUILD_TESTS) 里才创建。
if(WIN32)
  set(_vcpkg_bin_dirs
      "${VCPKG_INSTALLED_DIR}/bin"
      "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin")
  set(_targets genealogy-server)
  if(BUILD_TESTS)
    list(APPEND _targets genealogy-server-tests)
  endif()
  foreach(_target ${_targets})
    foreach(_dir ${_vcpkg_bin_dirs})
      if(EXISTS "${_dir}")
        file(GLOB _dlls "${_dir}/*.dll")
        if(_dlls)
          add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_dlls} $<TARGET_FILE_DIR:${_target}>
            COMMENT "拷贝 vcpkg DLL 到 ${_target} 旁边")
        endif()
      endif()
    endforeach()
  endforeach()
endif()
