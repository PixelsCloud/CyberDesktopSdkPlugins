cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopBrickClock VERSION 1.0.1 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_brick_sdk_tool BRICK_LICENSE)
cd_plugin_canvas_license(box2d BRICK_BOX_LICENSE)
set(BRICK_TOOL cd_brick_sdk_tool)

add_library(cd_brick_clock_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_include_directories(cd_brick_clock_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_brick_clock_core PUBLIC CyberDesktopSDK::SDK CyberCanvas::Physics2D)
set(id "com.cyberdesktop.clock.brick.1x4")
add_library(cd_brick_clock MODULE Plugin.cpp)
target_link_libraries(cd_brick_clock PRIVATE cd_brick_clock_core)
target_compile_definitions(cd_brick_clock PRIVATE CD_PLUGIN_BUILD)
target_link_options(cd_brick_clock PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_brick_clock PROPERTIES PREFIX "" OUTPUT_NAME plugin
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}")

set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
cd_asset_target(cd_brick_clock_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/brick-clock-packages"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cd_brick_clock>" "${input}/plugin.dll"
  COMMAND $<TARGET_FILE:${BRICK_TOOL}> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/1x4/plugin.textproto" "${input}/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/1x4/preview.png" "${input}/preview.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BRICK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BRICK_BOX_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:cd_brick_clock>"
  COMMAND $<TARGET_FILE:${BRICK_TOOL}> pack "${input}" "${CMAKE_BINARY_DIR}/brick-clock-packages/${id}.cdpkg"
  DEPENDS cd_brick_clock ${BRICK_TOOL} "${CMAKE_CURRENT_SOURCE_DIR}/packages/1x4/plugin.textproto" VERBATIM)

add_executable(cd_brick_clock_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
add_executable(cd_brick_clock_design EXCLUDE_FROM_ALL tools/Design.cpp)
add_executable(cd_brick_clock_benchmark EXCLUDE_FROM_ALL tools/Benchmark.cpp)
foreach(target cd_brick_clock_core cd_brick_clock cd_brick_clock_tests cd_brick_clock_design cd_brick_clock_benchmark)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
target_link_libraries(cd_brick_clock_tests PRIVATE cd_brick_clock_core)
target_link_libraries(cd_brick_clock_design PRIVATE cd_brick_clock_core)
target_link_libraries(cd_brick_clock_benchmark PRIVATE cd_brick_clock_core)
