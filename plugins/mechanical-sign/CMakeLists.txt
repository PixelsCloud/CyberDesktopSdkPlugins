cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
set(sign_id com.cyberdesktop.sign.mechanical.6x2)
add_library(cd_mechanical_sign_core STATIC Model.cpp Renderer.cpp Instance.cpp Layout.cpp)
target_include_directories(cd_mechanical_sign_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
# ICU's Windows system API provides grapheme boundaries without bundling a second
# Unicode engine or treating UTF-16 code units as complete characters.
target_link_libraries(cd_mechanical_sign_core PUBLIC CyberDesktopSDK::SDK icu)
add_library(cd_mechanical_sign MODULE Plugin.cpp)
target_link_libraries(cd_mechanical_sign PRIVATE cd_mechanical_sign_core)
target_compile_definitions(cd_mechanical_sign PRIVATE CD_PLUGIN_BUILD)
target_link_options(cd_mechanical_sign PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_mechanical_sign PROPERTIES PREFIX "" OUTPUT_NAME plugin
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/link"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${sign_id}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${sign_id}")
set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${sign_id}")
cd_asset_target(cd_mechanical_sign_package ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/mechanical-sign-packages"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cd_mechanical_sign>" "${input}/plugin.dll"
  COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/6x2/plugin.textproto" "${input}/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/6x2/preview.png" "${input}/preview.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${input}/assets"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:cd_mechanical_sign>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/approved-body.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/provenance.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:cd_mechanical_sign>"
  COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mechanical-sign-packages/${sign_id}.cdpkg"
  DEPENDS cd_mechanical_sign cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/6x2/plugin.textproto" VERBATIM)
foreach(tool tests design)
  if(tool STREQUAL "tests")
    set(source tools/Tests.cpp)
  else()
    set(source tools/Design.cpp)
  endif()
  add_executable(cd_mechanical_sign_${tool} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(cd_mechanical_sign_${tool} PRIVATE cd_mechanical_sign_core)
  target_compile_definitions(cd_mechanical_sign_${tool} PRIVATE CD_SIGN_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
if(TARGET cd_sdk_support)
    target_link_libraries(cd_mechanical_sign_tests PRIVATE cd_sdk_support)
else()
    set_target_properties(cd_mechanical_sign_tests PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
foreach(target cd_mechanical_sign_core cd_mechanical_sign cd_mechanical_sign_tests cd_mechanical_sign_design)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

add_library(cd_mechanical_sign_testhost_test_support STATIC EXCLUDE_FROM_ALL tools/TestHost.cpp)
target_link_libraries(cd_mechanical_sign_testhost_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_mechanical_sign_testhost_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CD_PLUGIN_SOURCE_ROOT}")
target_compile_options(cd_mechanical_sign_testhost_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_sign_testhost_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_mechanical_sign_tests PRIVATE cd_mechanical_sign_testhost_test_support)

target_compile_definitions(cd_mechanical_sign_testhost_test_support PRIVATE CD_SIGN_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
