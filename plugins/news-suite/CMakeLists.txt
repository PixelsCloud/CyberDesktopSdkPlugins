cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopNews VERSION 1.0.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "News plugins require Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
find_package(nlohmann_json 3.12 CONFIG REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_news_sdk CD_NEWS_LICENSE)
cd_plugin_canvas_license(tweeny CD_NEWS_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_NEWS_YOGA_LICENSE)
include("${CMAKE_CURRENT_LIST_DIR}/../common/ContentText.cmake")
add_library(cd_news_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_link_libraries(cd_news_core PUBLIC CyberDesktopSDK::SDK cd_content_text PRIVATE nlohmann_json::nlohmann_json d2d1 dwrite windowscodecs ole32 shell32 user32)
target_include_directories(cd_news_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_news_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_news_core PRIVATE /utf-8 /W4)
set_target_properties(cd_news_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
set(items "#pragma once\n#include <array>\n#include \"plugins/news-suite/News.h\"\nnamespace news {\nstruct CatalogItem {Spec spec; const wchar_t* folder;};\ninline constexpr std::array<CatalogItem, ${count}> catalog{{\n")
foreach(index RANGE 0 ${last})
  foreach(field IN ITEMS id folder kind rows columns version)
    string(JSON ${field} GET "${catalog}" ${index} ${field})
  endforeach()
  string(APPEND items "{{\"${id}\", Kind(${kind}), ${rows}, ${columns}}, L\"${folder}\"},\n")
  string(REPLACE "-" "_" target "cd_news_${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_news_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_NEWS_ID="${id}" CD_NEWS_VERSION="${version}" CD_NEWS_KIND=${kind} CD_NEWS_ROWS=${rows})
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/news-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
    COMMAND $<TARGET_FILE:cd_news_sdk> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${input}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_NEWS_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_NEWS_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_NEWS_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_news_sdk> pack "${input}" "${CMAKE_BINARY_DIR}/news-packages/${id}.cdpkg"
    DEPENDS ${target} cd_news_sdk "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" VERBATIM)
endforeach()
string(APPEND items "}};\n}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/NewsCatalog.h" CONTENT "${items}")
foreach(tool IN ITEMS Tests Design)
  string(TOLOWER ${tool} name)
  add_executable(cd_news_${name} EXCLUDE_FROM_ALL tools/${tool}.cpp)
  target_link_libraries(cd_news_${name} PRIVATE cd_news_core nlohmann_json::nlohmann_json)
  target_include_directories(cd_news_${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_compile_options(cd_news_${name} PRIVATE /utf-8 /W4)
  set_target_properties(cd_news_${name} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

add_library(cd_news_suite_samples_test_support STATIC EXCLUDE_FROM_ALL tools/Samples.cpp)
target_link_libraries(cd_news_suite_samples_test_support PUBLIC CyberDesktopSDK::SDK cd_news_core)
target_include_directories(cd_news_suite_samples_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}")
target_compile_options(cd_news_suite_samples_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_news_suite_samples_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_news_tests PRIVATE cd_news_suite_samples_test_support)

target_link_libraries(cd_news_design PRIVATE cd_news_suite_samples_test_support)

if(TARGET cd_sdk_tool_support)
    target_link_libraries(cd_news_tests PRIVATE cd_sdk_tool_support)
    target_compile_definitions(cd_news_tests PRIVATE CD_NEWS_TEST_SERVICES)
endif()
