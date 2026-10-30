cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CyberDesktopLunar VERSION 1.0.0 LANGUAGES CXX)
    find_package(CyberDesktopSDK 1.1 CONFIG REQUIRED)
endif()
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Lunar requires Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_lunar_sdk_tool CD_LUNAR_LICENSE)

file(GLOB CD_LUNAR_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sxtwl/src/*.cpp")
add_library(cd_lunar_calendar STATIC ${CD_LUNAR_VENDOR})
target_include_directories(cd_lunar_calendar PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sxtwl/src")
target_compile_options(cd_lunar_calendar PRIVATE /utf-8 /w)
target_compile_definitions(cd_lunar_calendar PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(cd_lunar_calendar PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_library(cd_lunar_core STATIC Calendar.cpp Renderer.cpp Instance.cpp)
target_link_libraries(cd_lunar_core PUBLIC CyberDesktopSDK::SDK PRIVATE cd_lunar_calendar d2d1 dwrite windowscodecs ole32)
target_include_directories(cd_lunar_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_lunar_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_lunar_core PRIVATE /utf-8 /W4)
set_target_properties(cd_lunar_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

foreach(size IN ITEMS 1x2 2x2 4x4)
    set(id "com.cyberdesktop.calendar.lunar.${size}")
    set(target "cd_lunar_${size}")
    string(SUBSTRING ${size} 0 1 rows)
    string(SUBSTRING ${size} 2 1 columns)
    set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_lunar_core)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_LUNAR_ID="${id}" CD_LUNAR_ROWS=${rows} CD_LUNAR_COLUMNS=${columns})
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${size}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${size}")
    cd_asset_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/lunar-packages" "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
        COMMAND $<TARGET_FILE:cd_lunar_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "${input}/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "${input}/preview.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_LUNAR_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${input}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/provenance.json" "${input}/provenance.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND $<TARGET_FILE:cd_lunar_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/lunar-packages/${id}.cdpkg"
        DEPENDS ${target} cd_lunar_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" VERBATIM)
endforeach()
add_custom_target(cd_lunar_package DEPENDS cd_lunar_1x2_package cd_lunar_2x2_package cd_lunar_4x4_package)

add_executable(cd_lunar_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
add_executable(cd_lunar_design EXCLUDE_FROM_ALL tools/Design.cpp)
foreach(target cd_lunar_tests cd_lunar_design)
    target_link_libraries(${target} PRIVATE cd_lunar_core)
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

# Implementations shared by this plugin family.
target_sources(cd_lunar_core PRIVATE MechanicalLunar.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_lunar_core PUBLIC cd_mechanical_art)
