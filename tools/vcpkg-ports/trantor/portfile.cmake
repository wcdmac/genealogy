vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO an-tao/trantor
    REF "v${VERSION}"
    SHA512 12944d5acf2d4e9b1106319080f31515aa46ba6eed4339d960bc79b10576cf4ff5d86e70687632f1efe4ccf1f4f6b6049ca181df8532428f66f2a1071f3412d5
    HEAD_REF master
    PATCHES
        000-fix-deps.patch
        001-disable-werror.patch
)

set(feature_options)
if("spdlog" IN_LIST FEATURES)
    list(APPEND feature_options "-DUSE_SPDLOG=ON")
else()
    list(APPEND feature_options "-DUSE_SPDLOG=OFF")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${feature_options}
        # 钉死无 TLS。默认 ''（auto-detect）会在端口构建期碰巧找到系统
        # OpenSSL——Windows runner 的 mingw64、macOS 的 Homebrew——把 TLS
        # 代码编进端口并让安装出的 TrantorConfig 以 if(OpenSSL_FOUND)
        # 要求消费方也找得到 OpenSSL；本地开发机找不到时反而 if(FALSE)。
        # 同一份 overlay 在不同机器上装出不同语义不可接受，且本项目
        # 服务只绑回环不需要 TLS（2026-11-14 CI 三平台实测）。
        -DTRANTOR_USE_TLS=none
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/Trantor)

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/License")

vcpkg_copy_pdbs()
