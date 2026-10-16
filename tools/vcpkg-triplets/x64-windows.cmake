set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

# 本机 overlay。三条设置都为绕开本机实测的坑，理由见各处注释。
# 注意：overlay 目录里的同名文件会整体顶替 vcpkg 自带的 triplet，
# 故这里必须从 vcpkg/triplets/x64-windows.cmake 完整拷贝后再追加。

# ① 只编 Release：debug 配置本项目用不上，单并发下编两份要半小时以上。
set(VCPKG_BUILD_TYPE release)

# ② 关掉 /MP：vcpkg 的 windows.cmake 工具链给所有端口追加 /MP
#    （scripts/toolchains/windows.cmake 的 MP_BUILD_FLAG），与 ninja -jN
#    并行 + 同一 target 共享 /Fd PDB 撞成经典 MSVC 死锁——cl.exe 挂住
#    十几分钟 CPU 为 0（2026-10-15 实测 trantor：81 个 obj 后冻住 20 小时）。
#    这里写 /MP1（而不是 /MP-）：本机 MSVC 14.51 不接受 /MP-，
#    报 D8021「无效的数值参数」；/MP1 是同一意图的合法写法
#    （最多 1 个编译子进程 = 串行），且拼在 /MP 之后、后者生效。
#    两个变量必须同时给：vcpkg_configure_cmake.cmake 要求二者同设同不设。
set(VCPKG_CXX_FLAGS "/MP1")
set(VCPKG_C_FLAGS "/MP1")
