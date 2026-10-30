# Also buildable from an installed developer kit without Qt/protobuf.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.21)
  project(CyberDesktopExamples VERSION 1.4.0 LANGUAGES CXX)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
  if(NOT TARGET CyberDesktopSDK::Tool)
    message(FATAL_ERROR "The SDK must include cd-sdk")
  endif()
  add_executable(cd_sdk_cli ALIAS CyberDesktopSDK::Tool)
endif()
# Reference examples are independent native DLLs, never part of the desktop.
if(NOT COMMAND cyberdesktop_add_plugin)
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CyberDesktopPlugin.cmake")
endif()
add_subdirectory(workbench)
if(TARGET CyberCanvas::PhysicsScene)
  add_subdirectory(physics-sandbox)
endif()
if(TARGET CyberCanvas::Vector)
  add_subdirectory(rendering-gallery)
endif()
add_library(cd_sdk_example_hello MODULE ../templates/minimal/Plugin.cpp)
target_link_libraries(cd_sdk_example_hello PRIVATE CyberDesktopSDK::SDK)
target_compile_definitions(cd_sdk_example_hello PRIVATE CD_PLUGIN_BUILD)
target_compile_options(cd_sdk_example_hello PRIVATE /utf-8)
set_target_properties(cd_sdk_example_hello PROPERTIES PREFIX "" OUTPUT_NAME plugin
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hello"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hello"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/hello")
add_custom_command(TARGET cd_sdk_example_hello POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${CMAKE_CURRENT_SOURCE_DIR}/../templates/minimal/plugin.textproto" "$<TARGET_FILE_DIR:cd_sdk_example_hello>/plugin.textproto")

add_library(cd_sdk_example_spectrum MODULE SamplePlugin.cpp Drawing.cpp)
target_link_libraries(cd_sdk_example_spectrum PRIVATE CyberDesktopSDK::SDK gdi32 user32 dwrite)
target_compile_definitions(cd_sdk_example_spectrum PRIVATE CD_PLUGIN_BUILD CD_SAMPLE_KIND=4 CD_SAMPLE_ID="com.cyberdesktop.spectrum")
target_compile_options(cd_sdk_example_spectrum PRIVATE /utf-8)
set_target_properties(cd_sdk_example_spectrum PROPERTIES PREFIX "" OUTPUT_NAME plugin
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spectrum"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spectrum"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/spectrum")
add_custom_command(TARGET cd_sdk_example_spectrum POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${CMAKE_CURRENT_SOURCE_DIR}/spectrum/plugin.textproto" "$<TARGET_FILE_DIR:cd_sdk_example_spectrum>/plugin.textproto")

add_library(cd_sdk_example_hexclock MODULE SamplePlugin.cpp Drawing.cpp)
target_link_libraries(cd_sdk_example_hexclock PRIVATE CyberDesktopSDK::SDK gdi32 user32 dwrite)
target_compile_definitions(cd_sdk_example_hexclock PRIVATE CD_PLUGIN_BUILD CD_SAMPLE_KIND=5 CD_SAMPLE_ID="com.cyberdesktop.hexclock")
target_compile_options(cd_sdk_example_hexclock PRIVATE /utf-8)
set_target_properties(cd_sdk_example_hexclock PROPERTIES PREFIX "" OUTPUT_NAME plugin
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hexclock"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hexclock"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/hexclock")
if(CD_BUILTIN_HEXCLOCK_OUTPUT)
  set_target_properties(cd_sdk_example_hexclock PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CD_BUILTIN_HEXCLOCK_OUTPUT}"
    LIBRARY_OUTPUT_DIRECTORY "${CD_BUILTIN_HEXCLOCK_OUTPUT}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin-build/hexclock")
endif()
add_custom_command(TARGET cd_sdk_example_hexclock POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${CMAKE_CURRENT_SOURCE_DIR}/hexclock/plugin.textproto" "$<TARGET_FILE_DIR:cd_sdk_example_hexclock>/plugin.textproto")

set(data_kind 0)
foreach(name input-stats data-dashboard media-spectrum)
  string(REPLACE "-" "_" target_name "${name}")
  set(target "cd_sdk_example_${target_name}")
  add_library(${target} MODULE DataPlugin.cpp Drawing.cpp)
  target_link_libraries(${target} PRIVATE CyberDesktopSDK::SDK gdi32 user32 dwrite)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_DATA_SAMPLE_KIND=${data_kind}
    CD_DATA_SAMPLE_ID="org.cyberdesktop.sdk.${name}")
  target_compile_options(${target} PRIVATE /utf-8)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${name}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${name}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${name}")
  add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/${name}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto")
  math(EXPR data_kind "${data_kind}+1")
endforeach()

if(TARGET cd_sdk_cli)
  foreach(name hello spectrum hexclock input-stats data-dashboard media-spectrum)
    string(REPLACE "-" "_" target_name "${name}")
    if(name STREQUAL "hello")
      set(descriptor "${CMAKE_CURRENT_SOURCE_DIR}/../templates/minimal/plugin.textproto")
    else()
      set(descriptor "${CMAKE_CURRENT_SOURCE_DIR}/${name}/plugin.textproto")
    endif()
    cd_asset_target(cd_sdk_descriptor_${name} ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_sdk_example_${target_name}>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${descriptor}" "$<TARGET_FILE_DIR:cd_sdk_example_${target_name}>/plugin.textproto"
      COMMAND $<TARGET_FILE:cd_sdk_cli> descriptor "${descriptor}" "$<TARGET_FILE_DIR:cd_sdk_example_${target_name}>/descriptor.pb"
      DEPENDS cd_sdk_cli "${descriptor}" VERBATIM)
    add_dependencies(cd_sdk_descriptor_${name} cd_sdk_example_${target_name})
    if(name STREQUAL "hexclock" OR name STREQUAL "input-stats" OR name STREQUAL "data-dashboard" OR name STREQUAL "media-spectrum")
      cd_asset_target(cd_sdk_${target_name}_preview ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${name}/preview.png" "$<TARGET_FILE_DIR:cd_sdk_example_${target_name}>/preview.png" VERBATIM)
      add_dependencies(cd_sdk_descriptor_${name} cd_sdk_${target_name}_preview)
    endif()
  endforeach()
endif()

get_property(example_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS example_targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "MODULE_LIBRARY")
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    endif()
endforeach()
