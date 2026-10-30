cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopKoi VERSION 1.1.7 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1.1 CONFIG REQUIRED)
  find_package(CyberCanvas 1.2 CONFIG REQUIRED COMPONENTS Noise)
endif()
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Koi plugins require Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_koi_sdk_tool CD_KOI_SDK_LICENSE)
cd_plugin_canvas_license(tweeny CD_KOI_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_KOI_YOGA_LICENSE)
cd_plugin_canvas_license(fastnoiselite CD_KOI_NOISE_LICENSE)

add_library(cd_koi_core STATIC Simulation.cpp Renderer.cpp FishArtwork.cpp Instance.cpp)
target_link_libraries(cd_koi_core PUBLIC CyberDesktopSDK::SDK PRIVATE CyberCanvas::Noise CyberCanvas::Text CyberCanvas::Effects d2d1 dwrite windowscodecs ole32)
target_include_directories(cd_koi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_koi_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_koi_core PRIVATE /utf-8 /W4)
set_target_properties(cd_koi_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

foreach(size IN ITEMS 2x4 4x4 3x6)
  string(SUBSTRING ${size} 0 1 rows)
  string(SUBSTRING ${size} 2 1 columns)
  if(size STREQUAL "2x4")
    set(count 5)
  elseif(size STREQUAL "4x4")
    set(count 8)
  else()
    set(count 9)
  endif()
  set(id "com.cyberdesktop.koi.${size}")
  set(target "cd_koi_${size}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_koi_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_KOI_ID="${id}" CD_KOI_ROWS=${rows} CD_KOI_COLUMNS=${columns} CD_KOI_COUNT=${count})
  target_compile_options(${target} PRIVATE /utf-8)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${size}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${size}")
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/koi-packages" "$<TARGET_FILE_DIR:${target}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
    COMMAND $<TARGET_FILE:cd_koi_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/provenance.json" "${input}/provenance.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${input}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_KOI_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_KOI_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_KOI_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_KOI_NOISE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_koi_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/koi-packages/${id}.cdpkg"
    DEPENDS ${target} cd_koi_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" VERBATIM)
endforeach()

add_executable(cd_koi_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
add_executable(cd_koi_design EXCLUDE_FROM_ALL tools/Design.cpp)
foreach(target cd_koi_tests cd_koi_design)
  target_link_libraries(${target} PRIVATE cd_koi_core)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
