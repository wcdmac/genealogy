# M3：桌面 GUI（Dear ImGui docking 分支 + GLFW，MIT / zlib）。
#
# 启用方式（需 vcpkg feature "gui"）：
#   vcpkg install --x-feature=gui genealogy
#   cmake -S . -B build -DBUILD_GUI=ON -DVCPKG_MANIFEST_FEATURES=gui ...
#
# 设计文档 3.4 的面板清单：人物卡 / 世系图画布 / 世系表 / 检索 / 事件时间线 /
# 迁徙地图 / 证据 / 校验。渲染后端 OpenGL 3.2+ / GLSL 130（Win/Linux 通用）。
#
# 为什么 ImGui 是 vendor 进 third_party/ 而不是设计文档原定的 FetchContent：
# 本机到 github.com 的 TLS 连接会随机抽风（curl 35，实测重试 5 次才成功），
# FetchContent 在 configure 期下载，一失败整个 configure 挂死，而且失败
# 不可预期、不可重试提示。GUI 框架版本相对稳定，vendor 换来确定性构建。
# 升级方式：把新 tarball 的 15 个必需文件换进来即可（清单见下方源文件列表）。
# LICENSE（MIT）随源码一同入库，满足许可证白名单要求。

# ---------------------------------------------------------------- ImGui
# 只编运行必需的文件：核心 4 个 cpp + imconfig + imstb 三个头 + 两个后端。
# demo.cpp 不编（发布产物不需要）；examples/ 与 docs/ 整个不拷。
add_library(imgui STATIC
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp
  ${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
  ${PROJECT_SOURCE_DIR}/third_party/imgui
  ${PROJECT_SOURCE_DIR}/third_party/imgui/backends
)

# docking 分支 1.93 起部分代码用 C++17 特性
target_compile_features(imgui PUBLIC cxx_std_17)

# 目标名是 glfw 不是 glfw3：vcpkg 的 glfw3Config.cmake 里
# add_library(glfw SHARED IMPORTED)。写成 glfw3 的话 CMake 不报错，
# 但会把它当裸库名——编译期缺 GLFW/glfw3.h、链接期缺 glfw3.lib，
# 两个错误都离真实原因（名字错）很远。
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(imgui PUBLIC glfw)

if(WIN32)
  # GLFW 的 vcpkg 端口是 DLL 形态（glfw3dll.lib + glfw3.dll）。
  # OpenGL 只走 opengl32（系统自带），无需额外链接。
  target_link_libraries(imgui PUBLIC opengl32)
elseif(APPLE)
  find_package(OpenGL REQUIRED)
  target_link_libraries(imgui PUBLIC OpenGL::GL)
  target_link_libraries(imgui PUBLIC "-framework Cocoa -framework IOKit")
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(imgui PUBLIC OpenGL::GL)
  find_package(Threads REQUIRED)
  target_link_libraries(imgui PUBLIC Threads::Threads dl)
endif()

# ---------------------------------------------------------------- 主程序
add_executable(genealogy-gui
  src/main.cpp
  src/app.cpp
  src/db_session.cpp
  src/font.cpp
  src/panels/person_card.cpp
  src/panels/person_table.cpp
  src/panels/search_panel.cpp
  src/panels/relation_panel.cpp
  src/panels/validation_panel.cpp
  src/panels/tree_panel.cpp
)

target_include_directories(genealogy-gui
  PRIVATE ${PROJECT_SOURCE_DIR}/gui/include
)

target_link_libraries(genealogy-gui
  PRIVATE
    genealogy-core
    imgui
)

# 分发：GUI 直接以可执行文件形式进包（非 .app bundle）——GLFW 窗口
# 不需要 bundle 结构，M6 的验收是「产物可安装、GUI 可启动」。
# Windows 运行时 DLL 由根 CMakeLists 的 install(CODE) 从 build/bin 收拢。
install(TARGETS genealogy-gui RUNTIME DESTINATION bin)

# ---------------------------------------------------------------- 测试
# 无头测试的覆盖面：DbSession（GUI 调 core 用例的同一条入口链）。
# 面板装配函数虽不调 ImGui，但与 ImGui 绘制同文件同类、且私有于面板——
# 要进无头测试需先把装配抽到无 UI 头的 TU（登记为 ROADMAP 暂缓项），
# 此前「面板状态刷新可无头单测」的说法超前于事实。
# 渲染层（GLFW/OpenGL）需要显示环境，只保证编译通过与运行时报错路径。
if(BUILD_TESTS)
  find_package(Catch2 CONFIG REQUIRED)
  add_executable(genealogy-gui-tests
    tests/test_gui.cpp
    # DbSession 是 GUI 数据层的入口，编进测试二进制才能无头单测。
    # 它只依赖 core（无 ImGui/GLFW 头文件），链接进来不会拖入渲染栈。
    ${PROJECT_SOURCE_DIR}/gui/src/db_session.cpp
  )
  target_include_directories(genealogy-gui-tests
    PRIVATE ${PROJECT_SOURCE_DIR}/gui/include
  )
  target_link_libraries(genealogy-gui-tests
    PRIVATE
      genealogy-core
      Catch2::Catch2WithMain
  )
  add_test(NAME genealogy-gui-tests COMMAND genealogy-gui-tests)
endif()

# ---------------------------------------------------------------- DLL 分发
# Windows 上 GLFW 与 Drogon 全家都是 DLL。加载器按「exe 目录 → PATH」
# 查找，ctest 直接起 exe 时调用方 PATH 里未必有 vcpkg bin——
# 拷到 exe 旁一劳永逸（与 server/CMakeLists.txt 同口径）。
if(WIN32)
  set(_vcpkg_bin_dirs
      "${VCPKG_INSTALLED_DIR}/bin"
      "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin")
  set(_targets genealogy-gui)
  if(BUILD_TESTS)
    list(APPEND _targets genealogy-gui-tests)
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
