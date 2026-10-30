cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopAmbience VERSION 1.0.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1.1 CONFIG REQUIRED)
  find_package(CyberCanvas 1.2 CONFIG REQUIRED COMPONENTS Noise)
endif()
set(CMAKE_CXX_STANDARD 20)
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Ambience requires Windows x64 MSVC /MT")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_ambience_sdk CD_AMBIENCE_LICENSE)
cd_plugin_canvas_license(tweeny CD_AMBIENCE_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_AMBIENCE_YOGA_LICENSE)
cd_plugin_canvas_license(fastnoiselite CD_AMBIENCE_NOISE_LICENSE)
add_library(cd_ambience_core STATIC Model.cpp Renderer.cpp Instance.cpp NoiseScene.cpp)
target_link_libraries(cd_ambience_core PUBLIC CyberDesktopSDK::SDK PRIVATE CyberCanvas::Noise CyberCanvas::Effects d2d1 dwrite windowscodecs ole32)
target_include_directories(cd_ambience_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_ambience_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_ambience_core PRIVATE /utf-8 /W4)
set_target_properties(cd_ambience_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
set(items "#pragma once\n#include <array>\n#include \"Atmosphere.h\"\nnamespace atmosphere {\nstruct CatalogItem {Spec spec; const wchar_t* folder;};\ninline constexpr std::array<CatalogItem, ${count}> catalog{{\n")
foreach(index RANGE ${last})
  foreach(key id folder kind rows columns version)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(APPEND items "{{\"${id}\", Kind(${kind}), ${rows}, ${columns}}, L\"${folder}\"},\n")
  string(REPLACE "-" "_" target "cd_ambience_${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_ambience_core)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_AMBIENCE_ID="${id}" CD_AMBIENCE_VERSION="${version}" CD_AMBIENCE_KIND=${kind} CD_AMBIENCE_ROWS=${rows} CD_AMBIENCE_COLUMNS=${columns})
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(resource_dependency)
  if(kind EQUAL 2)
    cd_asset_target(${target}_assets
      COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets/noise"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/noise" "${input}/assets/noise"
      VERBATIM)
    set(resource_dependency ${target}_assets)
  endif()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/ambience-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
    COMMAND $<TARGET_FILE:cd_ambience_sdk> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_AMBIENCE_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_AMBIENCE_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_AMBIENCE_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_AMBIENCE_NOISE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_ambience_sdk> pack "${input}" "${CMAKE_BINARY_DIR}/ambience-packages/${id}.cdpkg"
    DEPENDS ${target} cd_ambience_sdk ${resource_dependency} "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" VERBATIM)
endforeach()
string(APPEND items "}};\n}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/AmbienceCatalog.h" CONTENT "${items}")
add_executable(cd_ambience_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
add_executable(cd_ambience_design EXCLUDE_FROM_ALL tools/Design.cpp)
foreach(target cd_ambience_tests cd_ambience_design)
  target_link_libraries(${target} PRIVATE cd_ambience_core)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
