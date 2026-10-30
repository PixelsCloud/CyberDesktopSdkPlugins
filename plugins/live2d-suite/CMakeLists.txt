cmake_minimum_required(VERSION 3.26)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CyberDesktopLive2D LANGUAGES CXX)
    find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_live2d_sdk_tool CD_LIVE2D_SDK_LICENSE)
add_library(cd_live2d_plugin MODULE Plugin.cpp)
target_link_libraries(cd_live2d_plugin PRIVATE CyberDesktopSDK::SDK d2d1 dwrite windowscodecs ole32)
target_compile_definitions(cd_live2d_plugin PRIVATE CD_PLUGIN_BUILD NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_live2d_plugin PRIVATE /utf-8 /W4)
set_target_properties(cd_live2d_plugin PROPERTIES PREFIX "" OUTPUT_NAME plugin
  MSVC_RUNTIME_LIBRARY MultiThreaded AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.live2d"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.live2d")
set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input")
cd_asset_target(cd_live2d_plugin_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/live2d-packages"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cd_live2d_plugin>" "${input}/plugin.dll"
  COMMAND $<TARGET_FILE:cd_live2d_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto" "${input}/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" "${input}/THIRD_PARTY_NOTICES.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/models" "${input}/models"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${input}/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/models" "$<TARGET_FILE_DIR:cd_live2d_plugin>/models"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:cd_live2d_plugin>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_LIVE2D_SDK_LICENSE}" "${input}/licenses/SDK-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:cd_live2d_plugin>"
  COMMAND $<TARGET_FILE:cd_live2d_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/live2d-packages/com.cyberdesktop.live2d.cdpkg"
  DEPENDS cd_live2d_plugin cd_live2d_sdk_tool VERBATIM)

add_library(cd_live2d_presentation STATIC StatusPresentation.cpp)
target_include_directories(cd_live2d_presentation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_live2d_presentation PUBLIC CyberDesktopSDK::SDK)
target_compile_options(cd_live2d_presentation PRIVATE /utf-8 /W4)
set_target_properties(cd_live2d_presentation PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(cd_live2d_plugin PRIVATE cd_live2d_presentation)

target_compile_features(cd_live2d_plugin PRIVATE cxx_std_20)
