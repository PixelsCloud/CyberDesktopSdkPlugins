cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")

# Link the existing timer and Box2D sand model without changing the old plugins.
add_library(cd_mechanical_hourglass_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_link_libraries(cd_mechanical_hourglass_core PUBLIC cd_hourglass_core)
target_include_directories(cd_mechanical_hourglass_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_mechanical_hourglass_core PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_hourglass_core PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

set(packages)
foreach(size IN ITEMS 1x1 2x1)
  set(id "com.cyberdesktop.hourglass.mechanical.${size}")
  set(target "cd_mechanical_hourglass_${size}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_mechanical_hourglass_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MECHANICAL_HOURGLASS_ID="${id}")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${size}")
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses"
      "${CMAKE_BINARY_DIR}/mechanical-hourglass-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/chassis-${size}.png" "${input}/assets/body.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/glass.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/provenance.json" "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mechanical-hourglass-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${size}/plugin.textproto"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/chassis-${size}.png" "${CMAKE_CURRENT_SOURCE_DIR}/assets/glass.png" VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_mechanical_hourglass_packages DEPENDS ${packages})

foreach(tool tests design)
  if(tool STREQUAL "tests")
    set(source tools/Tests.cpp)
  else()
    set(source tools/Design.cpp)
  endif()
  add_executable(cd_mechanical_hourglass_${tool} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(cd_mechanical_hourglass_${tool} PRIVATE cd_mechanical_hourglass_core)
  target_compile_options(cd_mechanical_hourglass_${tool} PRIVATE /utf-8 /W4)
  set_target_properties(cd_mechanical_hourglass_${tool} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

add_library(cd_mechanical_hourglass_testhost_test_support STATIC EXCLUDE_FROM_ALL tools/TestHost.cpp)
target_link_libraries(cd_mechanical_hourglass_testhost_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_mechanical_hourglass_testhost_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CD_PLUGIN_SOURCE_ROOT}")
target_compile_options(cd_mechanical_hourglass_testhost_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_hourglass_testhost_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_mechanical_hourglass_tests PRIVATE cd_mechanical_hourglass_testhost_test_support)
