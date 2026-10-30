cmake_minimum_required(VERSION 3.26)
project(CyberDesktopStarter VERSION 1.0.0 LANGUAGES CXX)
find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
cyberdesktop_add_plugin(myplugin ID com.example.components
  DESCRIPTOR "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto"
  SOURCES Plugin.cpp Card.cpp ServiceModel.cpp ASSETS README.md LICENSE)
target_include_directories(myplugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(CD_SDK_PREFIX "${CyberDesktopSDK_DIR}/../../.." ABSOLUTE)
foreach(dependency yoga tweeny)
  set(notice "${CD_SDK_PREFIX}/share/cybercanvas/third_party/${dependency}/LICENSE")
  if(EXISTS "${notice}")
    add_custom_target(license_${dependency} ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:myplugin>/licenses"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${notice}" "$<TARGET_FILE_DIR:myplugin>/licenses/${dependency}-LICENSE.txt"
      DEPENDS myplugin VERBATIM)
    add_dependencies(myplugin_descriptor license_${dependency})
  endif()
endforeach()

target_compile_definitions(myplugin PRIVATE CD_COMPONENT_KIND=0)
