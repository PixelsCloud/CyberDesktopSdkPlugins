cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CyberDesktopHourglass VERSION 1.2.1 LANGUAGES CXX)
    find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
endif()
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Hourglass requires Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_hourglass_sdk_tool CD_HOURGLASS_LICENSE)
cd_plugin_canvas_license(box2d CD_HOURGLASS_BOX_LICENSE)

add_library(cd_hourglass_core STATIC Model.cpp Renderer.cpp Instance.cpp PhysicalSand.cpp)
target_link_libraries(cd_hourglass_core PUBLIC CyberDesktopSDK::SDK CyberCanvas::Physics2D PRIVATE d2d1 dwrite windowscodecs ole32)
target_include_directories(cd_hourglass_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_hourglass_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_hourglass_core PRIVATE /utf-8 /W4)
set_target_properties(cd_hourglass_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

foreach(size IN ITEMS 1x2 1x1)
    set(id "com.cyberdesktop.hourglass.pixel.${size}")
    set(target "cd_hourglass_${size}")
    set(show_timer 1)
    if(size STREQUAL "1x1")
        set(show_timer 0)
    endif()
    set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_hourglass_core)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_HOURGLASS_ID="${id}" CD_HOURGLASS_SHOW_TIMER=${show_timer})
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${size}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${size}")
    cd_asset_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/hourglass-packages" "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
        COMMAND $<TARGET_FILE:cd_hourglass_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "${input}/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "${input}/preview.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_HOURGLASS_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_HOURGLASS_BOX_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND $<TARGET_FILE:cd_hourglass_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/hourglass-packages/${id}.cdpkg"
        DEPENDS ${target} cd_hourglass_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" VERBATIM)
endforeach()
add_custom_target(cd_hourglass_package DEPENDS cd_hourglass_1x2_package cd_hourglass_1x1_package)

add_executable(cd_hourglass_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
add_executable(cd_hourglass_design EXCLUDE_FROM_ALL tools/Design.cpp)
foreach(target cd_hourglass_tests cd_hourglass_design)
    target_link_libraries(${target} PRIVATE cd_hourglass_core)
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
