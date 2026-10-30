install(TARGETS cd_sdk_abi cd_sdk_core cd_sdk_runtime EXPORT CyberDesktopSDKTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS cd_public_sdk cd_sdk_rendering cd_sdk_images cd_sdk_layout EXPORT CyberDesktopSDKRenderingTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${CD_SDK_HEADERS} "${CMAKE_CURRENT_BINARY_DIR}/include/sdk/Version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sdk)
file(GLOB_RECURSE CD_SDK_SERVICE_HEADERS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/services/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.h")
foreach(header IN LISTS CD_SDK_SERVICE_HEADERS)
    configure_file("${header}" "include/sdk/${header}" COPYONLY)
endforeach()
install(DIRECTORY services cpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/sdk")
configure_package_config_file(cmake/CyberDesktopSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/CyberDesktopSDKConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyberDesktopSDK")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CyberDesktopSDKConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT CyberDesktopSDKTargets NAMESPACE CyberDesktopSDK::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyberDesktopSDK")
install(EXPORT CyberDesktopSDKRenderingTargets NAMESPACE CyberDesktopSDK::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyberDesktopSDK")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CyberDesktopSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CyberDesktopSDKConfigVersion.cmake" cmake/CyberDesktopPlugin.cmake cmake/NativeCompilerDependencies.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyberDesktopSDK")
install(DIRECTORY schemas docs templates examples DESTINATION "${CMAKE_INSTALL_DATADIR}/cyberdesktop-sdk"
    PATTERN "clock" EXCLUDE PATTERN "counter" EXCLUDE PATTERN "sysinfo" EXCLUDE)
install(FILES LICENSE README.md CHANGELOG.md release.json public-source.json services.json DESTINATION "${CMAKE_INSTALL_DATADIR}/cyberdesktop-sdk")

install(DIRECTORY third_party DESTINATION "${CMAKE_INSTALL_DATADIR}/cyberdesktop-sdk")

install(DIRECTORY scripts DESTINATION "${CMAKE_INSTALL_DATADIR}/cyberdesktop-sdk")

install(FILES cmake/IncrementalAssets.cmake
    cmake/PruneGeneratedAssets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CyberDesktopSDK")
