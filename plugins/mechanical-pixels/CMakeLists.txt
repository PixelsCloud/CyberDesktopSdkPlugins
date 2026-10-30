cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")

add_library(cd_mechanical_pixels_core STATIC Model.cpp Renderer.cpp Instance.cpp Face.cpp)
target_link_libraries(cd_mechanical_pixels_core PUBLIC CyberDesktopSDK::SDK cd_widget_core)
target_include_directories(cd_mechanical_pixels_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_mechanical_pixels_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_mechanical_pixels_core PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_pixels_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
foreach(index RANGE 0 ${last})
  foreach(key id folder art)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_mechanical_pixels_${folder}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_mechanical_pixels_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MECHANICAL_PIXEL_ID="${id}")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses"
      "${CMAKE_BINARY_DIR}/mechanical-pixels-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/provenance.json"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}"
      "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mechanical-pixels-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png" VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_mechanical_pixels_packages DEPENDS ${packages})

add_executable(cd_mechanical_pixels_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
target_link_libraries(cd_mechanical_pixels_tests PRIVATE cd_mechanical_pixels_core)
target_compile_options(cd_mechanical_pixels_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_pixels_tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(cd_mechanical_pixels_design EXCLUDE_FROM_ALL tools/Design.cpp)
target_link_libraries(cd_mechanical_pixels_design PRIVATE cd_mechanical_pixels_core)
target_compile_options(cd_mechanical_pixels_design PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_pixels_design PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_mechanical_pixels_testhost_test_support STATIC EXCLUDE_FROM_ALL tools/TestHost.cpp)
target_link_libraries(cd_mechanical_pixels_testhost_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_mechanical_pixels_testhost_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CD_PLUGIN_SOURCE_ROOT}")
target_compile_options(cd_mechanical_pixels_testhost_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_pixels_testhost_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_mechanical_pixels_tests PRIVATE cd_mechanical_pixels_testhost_test_support)
