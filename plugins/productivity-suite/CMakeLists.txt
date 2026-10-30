cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopProductivity VERSION 1.0.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Productivity plugins require Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
find_package(nlohmann_json 3.12 CONFIG REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_productivity_sdk_tool CD_PRODUCTIVITY_LICENSE)
include("${CMAKE_CURRENT_LIST_DIR}/../common/ContentText.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../common/ProductivityVolume.cmake")
add_library(cd_productivity_core STATIC Model.cpp Renderer.cpp Instance.cpp ThemedController.cpp)
target_link_libraries(cd_productivity_core PUBLIC CyberDesktopSDK::SDK PRIVATE cd_productivity_volume nlohmann_json::nlohmann_json cd_content_text d2d1 dwrite windowscodecs ole32 shell32 user32)
target_include_directories(cd_productivity_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_productivity_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_productivity_core PRIVATE /utf-8 /W4)
set_target_properties(cd_productivity_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
foreach(index RANGE 0 ${last})
  foreach(field IN ITEMS id folder kind rows columns)
    string(JSON ${field} GET "${catalog}" ${index} ${field})
  endforeach()
  string(REPLACE "-" "_" key "${folder}")
  set(target "cd_productivity_${key}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_productivity_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_PRODUCTIVITY_ID="${id}" CD_PRODUCTIVITY_KIND=${kind} CD_PRODUCTIVITY_ROWS=${rows} CD_PRODUCTIVITY_COLUMNS=${columns})
  target_compile_options(${target} PRIVATE /utf-8)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${folder}")
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses" "${CMAKE_BINARY_DIR}/productivity-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "$<TARGET_FILE_DIR:${target}>/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:${target}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PRODUCTIVITY_LICENSE}" "$<TARGET_FILE_DIR:${target}>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "$<TARGET_FILE_DIR:${target}>/preview.png"
    COMMAND $<TARGET_FILE:cd_productivity_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/descriptor.pb"
    COMMAND $<TARGET_FILE:cd_productivity_sdk_tool> pack "$<TARGET_FILE_DIR:${target}>" "${CMAKE_BINARY_DIR}/productivity-packages/${id}.cdpkg"
    DEPENDS ${target} cd_productivity_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" VERBATIM)
endforeach()
foreach(tool IN ITEMS Design Tests)
  string(TOLOWER ${tool} name)
  add_executable(cd_productivity_${name} EXCLUDE_FROM_ALL tools/${tool}.cpp)
  target_link_libraries(cd_productivity_${name} PRIVATE cd_productivity_core nlohmann_json::nlohmann_json)
  target_compile_options(cd_productivity_${name} PRIVATE /utf-8 /W4)
  set_target_properties(cd_productivity_${name} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
if(TARGET Qt6::Widgets)
  find_package(Qt6 REQUIRED COMPONENTS Svg)
  add_executable(cd_productivity_rasterize EXCLUDE_FROM_ALL tools/Rasterize.cpp)
  target_link_libraries(cd_productivity_rasterize PRIVATE Qt6::Widgets Qt6::Svg)
  target_compile_options(cd_productivity_rasterize PRIVATE /utf-8)
  set_target_properties(cd_productivity_rasterize PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_productivity_core PUBLIC cd_mechanical_art)

add_executable(cd_themed_productivity_tests EXCLUDE_FROM_ALL tools/ThemedControllerTests.cpp)
target_link_libraries(cd_themed_productivity_tests PRIVATE cd_productivity_core)
target_compile_options(cd_themed_productivity_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_themed_productivity_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
enable_testing()
add_test(NAME themed_productivity_controller COMMAND cd_themed_productivity_tests)
set_tests_properties(themed_productivity_controller PROPERTIES TIMEOUT 30)

if(TARGET cd_sdk_tool_support)
    add_executable(cd_productivity_volume_tests EXCLUDE_FROM_ALL tools/VolumeTest.cpp)
    target_link_libraries(cd_productivity_volume_tests PRIVATE cd_productivity_core cd_sdk_tool_support)
    target_compile_options(cd_productivity_volume_tests PRIVATE /utf-8 /W4)
    set_target_properties(cd_productivity_volume_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()
