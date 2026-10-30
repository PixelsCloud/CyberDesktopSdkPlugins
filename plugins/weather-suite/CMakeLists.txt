cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopWeather VERSION 1.0.1 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Weather plugins require Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
find_package(nlohmann_json 3.12 CONFIG REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_weather_sdk_tool CD_WEATHER_SDK_LICENSE)
add_library(cd_weather_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_link_libraries(cd_weather_core PUBLIC CyberDesktopSDK::SDK PRIVATE d2d1 dwrite windowscodecs ole32)
target_include_directories(cd_weather_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_weather_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_weather_core PRIVATE /utf-8 /W4)
set_target_properties(cd_weather_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
foreach(size IN ITEMS 1x2 1x4 2x4)
  string(SUBSTRING ${size} 0 1 rows)
  string(SUBSTRING ${size} 2 1 columns)
  set(id "com.cyberdesktop.weather.${size}")
  set(target "cd_weather_${size}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_weather_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_WEATHER_ID="${id}" CD_WEATHER_ROWS=${rows} CD_WEATHER_COLUMNS=${columns})
  target_compile_options(${target} PRIVATE /utf-8)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${size}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${size}")
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses" "${CMAKE_BINARY_DIR}/weather-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "$<TARGET_FILE_DIR:${target}>/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:${target}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_WEATHER_SDK_LICENSE}" "$<TARGET_FILE_DIR:${target}>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "$<TARGET_FILE_DIR:${target}>/preview.png"
    COMMAND $<TARGET_FILE:cd_weather_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/descriptor.pb"
    COMMAND $<TARGET_FILE:cd_weather_sdk_tool> pack "$<TARGET_FILE_DIR:${target}>" "${CMAKE_BINARY_DIR}/weather-packages/${id}.cdpkg"
    DEPENDS ${target} cd_weather_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" VERBATIM)
endforeach()
add_executable(cd_weather_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
add_executable(cd_weather_design EXCLUDE_FROM_ALL tools/Design.cpp)
foreach(target cd_weather_tests cd_weather_design)
  target_link_libraries(${target} PRIVATE cd_weather_core nlohmann_json::nlohmann_json)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
if(TARGET Qt6::Widgets)
  find_package(Qt6 REQUIRED COMPONENTS Svg)
  add_executable(cd_weather_rasterize EXCLUDE_FROM_ALL tools/Rasterize.cpp)
  target_link_libraries(cd_weather_rasterize PRIVATE Qt6::Widgets Qt6::Svg)
  target_compile_options(cd_weather_rasterize PRIVATE /utf-8)
  set_target_properties(cd_weather_rasterize PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_weather_core PUBLIC cd_mechanical_art)
