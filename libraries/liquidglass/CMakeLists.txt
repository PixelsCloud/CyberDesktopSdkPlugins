cmake_minimum_required(VERSION 3.24)
project(CyberLiquidGlass VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(cyber_liquid_glass STATIC src/ReactParameters.cpp)
add_library(CyberGlass::Liquid ALIAS cyber_liquid_glass)
target_compile_features(cyber_liquid_glass PUBLIC cxx_std_20)
target_include_directories(cyber_liquid_glass PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
set_target_properties(cyber_liquid_glass PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF EXPORT_NAME Liquid)
if(WIN32)
    set(cd_react_maps "// Generated from the MIT licensed React displacement maps.\n")
    foreach(cd_map IN ITEMS Standard Polar Prominent)
        string(TOLOWER "${cd_map}" cd_map_file)
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/react-${cd_map_file}.rg" cd_react_map HEX)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/react-${cd_map_file}.rg")
        string(REGEX REPLACE "(..)" "0x\\1," cd_react_map "${cd_react_map}")
        string(APPEND cd_react_maps "constexpr unsigned char kReact${cd_map}[] = {${cd_react_map}};\n")
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/ReactDisplacement.h"
        CONTENT "${cd_react_maps}")
    set(cd_react_shader_header "${CMAKE_CURRENT_BINARY_DIR}/generated/ReactMaterialShader.h")
    add_custom_command(OUTPUT "${cd_react_shader_header}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/react/Material.hlsl"
            "-DOUTPUT=${cd_react_shader_header}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedReactShader.cmake"
        DEPENDS src/react/Material.hlsl cmake/EmbedReactShader.cmake VERBATIM)
    target_sources(cyber_liquid_glass PRIVATE "${cd_react_shader_header}")
    target_include_directories(cyber_liquid_glass PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_sources(cyber_liquid_glass PRIVATE src/DisplayCapture_win.cpp src/Changes_win.cpp
        src/GpuTimer_win.cpp src/GpuTimer_win.h src/react/Displacement.cpp src/react/Displacement.h
        src/react/ReactEffect_win.cpp)
    target_include_directories(cyber_liquid_glass PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    target_link_libraries(cyber_liquid_glass PRIVATE windowsapp dxguid d3d11 d3dcompiler d2d1 Magnification ole32)
    target_compile_definitions(cyber_liquid_glass PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()
if(MSVC)
    target_compile_options(cyber_liquid_glass PRIVATE /utf-8 /W4)
endif()
install(TARGETS cyber_liquid_glass EXPORT CyberLiquidGlassTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/CyberLiquidGlass/licenses" RENAME MIT.txt)
install(FILES resources/LiquidGlassReact-MIT.txt resources/README.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/CyberLiquidGlass/licenses")
install(EXPORT CyberLiquidGlassTargets NAMESPACE CyberGlass:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CyberLiquidGlass)
configure_package_config_file(cmake/CyberLiquidGlassConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/CyberLiquidGlassConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CyberLiquidGlass)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CyberLiquidGlassConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CyberLiquidGlassConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CyberLiquidGlassConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CyberLiquidGlass)
