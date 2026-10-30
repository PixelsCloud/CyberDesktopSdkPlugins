# Based on the ThorVG port at vcpkg baseline b216ddff25a1f432870e6c340ce79357049ef86e.
# Keep only the CPU renderer and content loaders used by CyberCanvas.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO thorvg/thorvg
    REF "v${VERSION}"
    SHA512 a3a1e3c84c2a0f6ff174adccdd1d185b4433cd781fed22a0e6b07f1ee7f7402ed220bc92caade4faa872362e7319491e532485f37e62c00e15cee5c829914c48
    HEAD_REF master
)
vcpkg_configure_meson(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dstatic=true
        -Dengines=['cpu']
        -Dloaders=['svg','lottie']
        -Dsavers=['']
        -Dsimd=true
        -Dbindings=['']
        -Dtools=['']
        -Dtests=false
        -Dstrip=false
        -Dextra=['']
        -Dthreads=false
    OPTIONS_DEBUG
        -Dlog=true
        -Dbindir=${CURRENT_PACKAGES_DIR}/debug/bin
    OPTIONS_RELEASE
        -Dbindir=${CURRENT_PACKAGES_DIR}/bin
)
vcpkg_install_meson()
vcpkg_fixup_pkgconfig()
vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/thorvg-1/thorvg.h" "#ifndef TVG_STATIC" "#if 0")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${SOURCE_PATH}/src/loaders/lottie/rapidjson/LICENSE")
