cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CyberDesktopSystemInfo VERSION 1.0.0 LANGUAGES CXX)
    find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "System Info plugins require Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CD_MONITOR_BUILD_TESTS "Build System Info focused checks and reference renderer" ${PROJECT_IS_TOP_LEVEL})
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_monitor_sdk_tool CD_MONITOR_LICENSE)
cd_plugin_canvas_license(tweeny CD_MONITOR_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_MONITOR_YOGA_LICENSE)

add_library(cd_monitor_core STATIC Monitor.cpp MonitorModel.cpp ThemedController.cpp MonitorRenderer.cpp)
target_link_libraries(cd_monitor_core PUBLIC CyberDesktopSDK::SDK PRIVATE CyberCanvas::Motion d2d1 dwrite windowscodecs ole32 user32)
target_include_directories(cd_monitor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_monitor_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_monitor_core PRIVATE /utf-8)
set_target_properties(cd_monitor_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
foreach(size 1x1 1x2 1x4 2x2)
    string(SUBSTRING "${size}" 0 1 rows)
    string(SUBSTRING "${size}" 2 1 columns)
    set(id "com.cyberdesktop.systeminfo.${size}")
    set(target "cd_monitor_${size}")
    set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_monitor_core)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MONITOR_ID="${id}" CD_MONITOR_ROWS=${rows} CD_MONITOR_COLUMNS=${columns})
    target_compile_options(${target} PRIVATE /utf-8)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${size}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${size}")
    cd_asset_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses"
        "${input}/licenses" "${CMAKE_BINARY_DIR}/system-info-packages"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "$<TARGET_FILE_DIR:${target}>/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MONITOR_LICENSE}" "$<TARGET_FILE_DIR:${target}>/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "$<TARGET_FILE_DIR:${target}>/preview.png"
        COMMAND $<TARGET_FILE:cd_monitor_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:${target}>/descriptor.pb" "${input}/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "${input}/preview.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MONITOR_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MONITOR_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MONITOR_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}/licenses" "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND $<TARGET_FILE:cd_monitor_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/system-info-packages/${id}.cdpkg"
        DEPENDS ${target} cd_monitor_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" VERBATIM)
endforeach()
foreach(name tests design)
    add_executable(cd_monitor_${name} EXCLUDE_FROM_ALL tools/${name}.cpp)
    target_link_libraries(cd_monitor_${name} PRIVATE cd_monitor_core)
    target_compile_options(cd_monitor_${name} PRIVATE /utf-8)
    set_target_properties(cd_monitor_${name} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
if(CD_MONITOR_BUILD_TESTS)
    enable_testing()
    set_target_properties(cd_monitor_tests cd_monitor_design PROPERTIES EXCLUDE_FROM_ALL FALSE)
    add_test(NAME system_info_plugin COMMAND cd_monitor_tests)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_monitor_core PUBLIC cd_mechanical_art)

add_executable(cd_themed_monitor_tests EXCLUDE_FROM_ALL tools/ThemedControllerTests.cpp)
target_link_libraries(cd_themed_monitor_tests PRIVATE cd_monitor_core)
target_compile_options(cd_themed_monitor_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_themed_monitor_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
enable_testing()
add_test(NAME themed_monitor_controller COMMAND cd_themed_monitor_tests)
set_tests_properties(themed_monitor_controller PROPERTIES TIMEOUT 30)
