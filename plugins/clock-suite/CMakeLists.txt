cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopClock VERSION 1.0.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Clock plugins target Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CD_CLOCK_BUILD_TESTS "Build focused Clock tests and design renderer" ${PROJECT_IS_TOP_LEVEL})
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_clock_sdk_tool CD_CLOCK_SDK_LICENSE)
add_library(cd_clock_core STATIC Clock.cpp ClockRenderer.cpp FlipClock.cpp FlipRenderer.cpp)
target_link_libraries(cd_clock_core PUBLIC CyberDesktopSDK::SDK PRIVATE d2d1 dwrite windowscodecs ole32)
target_include_directories(cd_clock_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_clock_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_clock_core PRIVATE /utf-8)
set_target_properties(cd_clock_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set(CD_CLOCK_PLUGIN_IDS "")
add_library(cd_clock_digital_1x1 MODULE Plugin.cpp)
target_link_libraries(cd_clock_digital_1x1 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_digital_1x1 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.digital.1x1" CD_CLOCK_ANALOG=0 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=1)
target_compile_options(cd_clock_digital_1x1 PRIVATE /utf-8)
target_link_options(cd_clock_digital_1x1 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_digital_1x1 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.1x1" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.1x1"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/digital-1x1" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/digital-1x1")
cd_asset_target(cd_clock_digital_1x1_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_digital_1x1>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x1/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_1x1>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x1/README.txt" "$<TARGET_FILE_DIR:cd_clock_digital_1x1>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_digital_1x1>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x1/preview.png" "$<TARGET_FILE_DIR:cd_clock_digital_1x1>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x1/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_1x1>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_digital_1x1>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.digital.1x1.cdpkg"
  DEPENDS cd_clock_digital_1x1 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x1/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.digital.1x1")
add_library(cd_clock_digital_1x2 MODULE Plugin.cpp)
target_link_libraries(cd_clock_digital_1x2 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_digital_1x2 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.digital.1x2" CD_CLOCK_ANALOG=0 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=2)
target_compile_options(cd_clock_digital_1x2 PRIVATE /utf-8)
target_link_options(cd_clock_digital_1x2 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_digital_1x2 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.1x2" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.1x2"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/digital-1x2" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/digital-1x2")
cd_asset_target(cd_clock_digital_1x2_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_digital_1x2>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_1x2>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x2/README.txt" "$<TARGET_FILE_DIR:cd_clock_digital_1x2>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_digital_1x2>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x2/preview.png" "$<TARGET_FILE_DIR:cd_clock_digital_1x2>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_1x2>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_digital_1x2>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.digital.1x2.cdpkg"
  DEPENDS cd_clock_digital_1x2 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x2/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.digital.1x2")
add_library(cd_clock_digital_1x4 MODULE Plugin.cpp)
target_link_libraries(cd_clock_digital_1x4 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_digital_1x4 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.digital.1x4" CD_CLOCK_ANALOG=0 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=4)
target_compile_options(cd_clock_digital_1x4 PRIVATE /utf-8)
target_link_options(cd_clock_digital_1x4 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_digital_1x4 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.1x4" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.1x4"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/digital-1x4" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/digital-1x4")
cd_asset_target(cd_clock_digital_1x4_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_digital_1x4>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_1x4>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x4/README.txt" "$<TARGET_FILE_DIR:cd_clock_digital_1x4>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_digital_1x4>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x4/preview.png" "$<TARGET_FILE_DIR:cd_clock_digital_1x4>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_1x4>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_digital_1x4>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.digital.1x4.cdpkg"
  DEPENDS cd_clock_digital_1x4 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-1x4/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.digital.1x4")
add_library(cd_clock_digital_2x2 MODULE Plugin.cpp)
target_link_libraries(cd_clock_digital_2x2 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_digital_2x2 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.digital.2x2" CD_CLOCK_ANALOG=0 CD_CLOCK_ROWS=2 CD_CLOCK_COLUMNS=2)
target_compile_options(cd_clock_digital_2x2 PRIVATE /utf-8)
target_link_options(cd_clock_digital_2x2 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_digital_2x2 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.2x2" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.digital.2x2"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/digital-2x2" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/digital-2x2")
cd_asset_target(cd_clock_digital_2x2_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_digital_2x2>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-2x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_2x2>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-2x2/README.txt" "$<TARGET_FILE_DIR:cd_clock_digital_2x2>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_digital_2x2>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-2x2/preview.png" "$<TARGET_FILE_DIR:cd_clock_digital_2x2>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-2x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_digital_2x2>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_digital_2x2>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.digital.2x2.cdpkg"
  DEPENDS cd_clock_digital_2x2 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/digital-2x2/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.digital.2x2")
add_library(cd_clock_analog_1x1 MODULE Plugin.cpp)
target_link_libraries(cd_clock_analog_1x1 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_analog_1x1 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.analog.1x1" CD_CLOCK_ANALOG=1 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=1)
target_compile_options(cd_clock_analog_1x1 PRIVATE /utf-8)
target_link_options(cd_clock_analog_1x1 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_analog_1x1 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.1x1" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.1x1"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/analog-1x1" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/analog-1x1")
cd_asset_target(cd_clock_analog_1x1_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_analog_1x1>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x1/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_1x1>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x1/README.txt" "$<TARGET_FILE_DIR:cd_clock_analog_1x1>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_analog_1x1>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x1/preview.png" "$<TARGET_FILE_DIR:cd_clock_analog_1x1>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x1/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_1x1>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_analog_1x1>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.analog.1x1.cdpkg"
  DEPENDS cd_clock_analog_1x1 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x1/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.analog.1x1")
add_library(cd_clock_analog_1x2 MODULE Plugin.cpp)
target_link_libraries(cd_clock_analog_1x2 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_analog_1x2 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.analog.1x2" CD_CLOCK_ANALOG=1 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=2)
target_compile_options(cd_clock_analog_1x2 PRIVATE /utf-8)
target_link_options(cd_clock_analog_1x2 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_analog_1x2 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.1x2" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.1x2"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/analog-1x2" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/analog-1x2")
cd_asset_target(cd_clock_analog_1x2_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_analog_1x2>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_1x2>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x2/README.txt" "$<TARGET_FILE_DIR:cd_clock_analog_1x2>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_analog_1x2>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x2/preview.png" "$<TARGET_FILE_DIR:cd_clock_analog_1x2>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_1x2>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_analog_1x2>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.analog.1x2.cdpkg"
  DEPENDS cd_clock_analog_1x2 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x2/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.analog.1x2")
add_library(cd_clock_analog_1x4 MODULE Plugin.cpp)
target_link_libraries(cd_clock_analog_1x4 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_analog_1x4 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.analog.1x4" CD_CLOCK_ANALOG=1 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=4)
target_compile_options(cd_clock_analog_1x4 PRIVATE /utf-8)
target_link_options(cd_clock_analog_1x4 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_analog_1x4 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.1x4" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.1x4"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/analog-1x4" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/analog-1x4")
cd_asset_target(cd_clock_analog_1x4_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_analog_1x4>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_1x4>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x4/README.txt" "$<TARGET_FILE_DIR:cd_clock_analog_1x4>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_analog_1x4>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x4/preview.png" "$<TARGET_FILE_DIR:cd_clock_analog_1x4>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_1x4>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_analog_1x4>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.analog.1x4.cdpkg"
  DEPENDS cd_clock_analog_1x4 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-1x4/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.analog.1x4")
add_library(cd_clock_analog_2x2 MODULE Plugin.cpp)
target_link_libraries(cd_clock_analog_2x2 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_analog_2x2 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.analog.2x2" CD_CLOCK_ANALOG=1 CD_CLOCK_ROWS=2 CD_CLOCK_COLUMNS=2)
target_compile_options(cd_clock_analog_2x2 PRIVATE /utf-8)
target_link_options(cd_clock_analog_2x2 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_analog_2x2 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.2x2" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.analog.2x2"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/analog-2x2" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/analog-2x2")
cd_asset_target(cd_clock_analog_2x2_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_analog_2x2>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-2x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_2x2>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-2x2/README.txt" "$<TARGET_FILE_DIR:cd_clock_analog_2x2>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_analog_2x2>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-2x2/preview.png" "$<TARGET_FILE_DIR:cd_clock_analog_2x2>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-2x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_analog_2x2>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_analog_2x2>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.analog.2x2.cdpkg"
  DEPENDS cd_clock_analog_2x2 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/analog-2x2/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.analog.2x2")
add_library(cd_clock_flip_2x2 MODULE Plugin.cpp)
target_link_libraries(cd_clock_flip_2x2 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_flip_2x2 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.flip.2x2" CD_CLOCK_ANALOG=0 CD_CLOCK_FLIP=1 CD_CLOCK_ROWS=2 CD_CLOCK_COLUMNS=2)
target_compile_options(cd_clock_flip_2x2 PRIVATE /utf-8)
target_link_options(cd_clock_flip_2x2 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_flip_2x2 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.flip.2x2" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.flip.2x2"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/flip-2x2" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/flip-2x2")
cd_asset_target(cd_clock_flip_2x2_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_flip_2x2>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_flip_2x2>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x2/README.txt" "$<TARGET_FILE_DIR:cd_clock_flip_2x2>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_flip_2x2>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x2/preview.png" "$<TARGET_FILE_DIR:cd_clock_flip_2x2>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x2/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_flip_2x2>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_flip_2x2>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.flip.2x2.cdpkg"
  DEPENDS cd_clock_flip_2x2 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x2/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.flip.2x2")
add_library(cd_clock_flip_1x4 MODULE Plugin.cpp)
target_link_libraries(cd_clock_flip_1x4 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_flip_1x4 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.flip.1x4" CD_CLOCK_ANALOG=0 CD_CLOCK_FLIP=1 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=4)
target_compile_options(cd_clock_flip_1x4 PRIVATE /utf-8)
target_link_options(cd_clock_flip_1x4 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_flip_1x4 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.flip.1x4" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.flip.1x4"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/flip-1x4" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/flip-1x4")
cd_asset_target(cd_clock_flip_1x4_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_flip_1x4>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-1x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_flip_1x4>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-1x4/README.txt" "$<TARGET_FILE_DIR:cd_clock_flip_1x4>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_flip_1x4>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-1x4/preview.png" "$<TARGET_FILE_DIR:cd_clock_flip_1x4>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-1x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_flip_1x4>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_flip_1x4>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.flip.1x4.cdpkg"
  DEPENDS cd_clock_flip_1x4 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-1x4/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.flip.1x4")
add_library(cd_clock_flip_2x4 MODULE Plugin.cpp)
target_link_libraries(cd_clock_flip_2x4 PRIVATE cd_clock_core)
target_compile_definitions(cd_clock_flip_2x4 PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="com.cyberdesktop.clock.flip.2x4" CD_CLOCK_ANALOG=0 CD_CLOCK_FLIP=1 CD_CLOCK_ROWS=2 CD_CLOCK_COLUMNS=4)
target_compile_options(cd_clock_flip_2x4 PRIVATE /utf-8)
target_link_options(cd_clock_flip_2x4 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_clock_flip_2x4 PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.flip.2x4" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.clock.flip.2x4"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/flip-2x4" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/flip-2x4")
cd_asset_target(cd_clock_flip_2x4_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_clock_flip_2x4>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_flip_2x4>/plugin.textproto"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x4/README.txt" "$<TARGET_FILE_DIR:cd_clock_flip_2x4>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:cd_clock_flip_2x4>/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x4/preview.png" "$<TARGET_FILE_DIR:cd_clock_flip_2x4>/preview.png"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x4/plugin.textproto" "$<TARGET_FILE_DIR:cd_clock_flip_2x4>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/clock-packages"
  COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:cd_clock_flip_2x4>" "${CMAKE_BINARY_DIR}/clock-packages/com.cyberdesktop.clock.flip.2x4.cdpkg"
  DEPENDS cd_clock_flip_2x4 cd_clock_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/flip-2x4/plugin.textproto" VERBATIM)
list(APPEND CD_CLOCK_PLUGIN_IDS "com.cyberdesktop.clock.flip.2x4")
foreach(family IN ITEMS lcd pixel pixelround)
  foreach(size IN ITEMS 1x2 1x4)
    string(SUBSTRING ${size} 2 1 columns)
    if(family STREQUAL "lcd")
      set(display 1)
    elseif(family STREQUAL "pixel")
      set(display 2)
    else()
      set(display 3)
    endif()
    set(id "com.cyberdesktop.clock.${family}.${size}")
    set(target "cd_clock_${family}_${size}")
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_clock_core)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_CLOCK_ID="${id}" CD_CLOCK_ANALOG=0 CD_CLOCK_ROWS=1 CD_CLOCK_COLUMNS=${columns} CD_CLOCK_DISPLAY=${display})
    target_compile_options(${target} PRIVATE /utf-8)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF PREFIX "" OUTPUT_NAME plugin
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
      ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${family}-${size}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${family}-${size}")
    cd_asset_target(${target}_package ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses" "${CMAKE_BINARY_DIR}/clock-packages"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${family}-${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${family}-${size}/README.txt" "$<TARGET_FILE_DIR:${target}>/README.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${family}-${size}/preview.png" "$<TARGET_FILE_DIR:${target}>/preview.png"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CLOCK_SDK_LICENSE}" "$<TARGET_FILE_DIR:${target}>/licenses/CyberDesktopSDK-MIT.txt"
      COMMAND $<TARGET_FILE:cd_clock_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${family}-${size}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/descriptor.pb"
      COMMAND $<TARGET_FILE:cd_clock_sdk_tool> pack "$<TARGET_FILE_DIR:${target}>" "${CMAKE_BINARY_DIR}/clock-packages/${id}.cdpkg"
      DEPENDS ${target} cd_clock_sdk_tool VERBATIM)
    list(APPEND CD_CLOCK_PLUGIN_IDS "${id}")
  endforeach()
endforeach()
add_executable(cd_electronic_clock_tests EXCLUDE_FROM_ALL tests/ElectronicClockTest.cpp)
target_link_libraries(cd_electronic_clock_tests PRIVATE cd_clock_core)
target_compile_options(cd_electronic_clock_tests PRIVATE /utf-8)
set_target_properties(cd_electronic_clock_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(CD_CLOCK_BUILD_TESTS)
  enable_testing()
  add_executable(cd_clock_tests tests/ClockTest.cpp)
  target_link_libraries(cd_clock_tests PRIVATE cd_clock_core)
  target_compile_options(cd_clock_tests PRIVATE /utf-8)
  set_target_properties(cd_clock_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  add_test(NAME clock_release COMMAND cd_clock_tests)
  add_executable(cd_flip_clock_tests tests/FlipClockTest.cpp)
  set_target_properties(cd_flip_clock_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  target_link_libraries(cd_flip_clock_tests PRIVATE cd_clock_core)
  target_compile_options(cd_flip_clock_tests PRIVATE /utf-8)
  set_target_properties(cd_flip_clock_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  add_test(NAME flip_clock_release COMMAND cd_flip_clock_tests)
  add_executable(cd_flip_clock_design tools/FlipClockDesign.cpp)
  target_link_libraries(cd_flip_clock_design PRIVATE cd_clock_core)
  target_compile_options(cd_flip_clock_design PRIVATE /utf-8)
  set_target_properties(cd_flip_clock_design PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  add_executable(cd_clock_design tools/ClockDesign.cpp)
  target_link_libraries(cd_clock_design PRIVATE cd_clock_core)
  target_compile_options(cd_clock_design PRIVATE /utf-8)
  set_target_properties(cd_clock_design PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()
