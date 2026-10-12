# M0 占位：桌面 GUI（Dear ImGui docking 分支 + GLFW，MIT / zlib）。
#
# 启用方式（需 vcpkg feature "gui"；Dear ImGui 经 FetchContent 引入）：
#   vcpkg install --x-feature=gui genealogy
#   cmake -S . -B build -DBUILD_GUI=ON ...
#
# M3 实现人物卡 / 世系表 / 自绘世系图画布 / 检索 / 证据 / 校验面板
# （设计文档 3.4）。ImGui 选型与 Qt 禁用理由见设计文档 2.3、2.6。
message(STATUS "gui: M0 占位模块，面板在 M3 实现")
