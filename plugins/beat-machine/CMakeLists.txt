cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopBeatMachine VERSION 1.1.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_beat_sdk_tool BEAT_LICENSE)
cd_plugin_canvas_license(box2d BEAT_BOX_LICENSE)
set(BEAT_TOOL cd_beat_sdk_tool)

add_library(cd_beat_machine_core STATIC AudioMotion.cpp Model.cpp Renderer.cpp Instance.cpp)
target_include_directories(cd_beat_machine_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_beat_machine_core PUBLIC CyberDesktopSDK::SDK CyberCanvas::Physics2D)
add_custom_target(cd_beat_machine_assets)
foreach(layout 6x2 4x2)
  set(beat_id com.cyberdesktop.music.beat-machine.${layout})
  set(module cd_beat_machine_${layout})
  add_library(${module} MODULE Plugin.cpp)
  target_link_libraries(${module} PRIVATE cd_beat_machine_core)
  target_compile_definitions(${module} PRIVATE CD_PLUGIN_BUILD CD_BEAT_ID="${beat_id}")
  target_link_options(${module} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${module} PROPERTIES PREFIX "" OUTPUT_NAME plugin
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/link/${layout}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${beat_id}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${beat_id}")
  cd_asset_target(cd_beat_machine_assets_${layout}
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "-DASSET_DIRECTORY=$<TARGET_FILE_DIR:${module}>/assets" "-DBUILD_ROOT=${CMAKE_BINARY_DIR}" "-DLAYOUT=${layout}" "-DKIND=beat"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../mechanical-instruments/StagePhysicsAssets.cmake"
    VERBATIM)
  add_dependencies(cd_beat_machine_assets cd_beat_machine_assets_${layout})
  add_dependencies(${module} cd_beat_machine_assets)
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${beat_id}")
  cd_asset_target(cd_beat_machine_package_${layout}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/beat-machine-packages"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${module}>" "${input}/plugin.dll"
  COMMAND $<TARGET_FILE:${BEAT_TOOL}> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${layout}/plugin.textproto" "${input}/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${layout}/preview.png" "${input}/preview.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEAT_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEAT_BOX_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
  COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "-DASSET_DIRECTORY=${input}/assets" "-DBUILD_ROOT=${CMAKE_BINARY_DIR}" "-DLAYOUT=${layout}" "-DKIND=beat"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../mechanical-instruments/StagePhysicsAssets.cmake"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${module}>"
  COMMAND $<TARGET_FILE:${BEAT_TOOL}> pack "${input}" "${CMAKE_BINARY_DIR}/beat-machine-packages/${beat_id}.cdpkg"
  DEPENDS ${module} ${BEAT_TOOL} "${CMAKE_CURRENT_SOURCE_DIR}/packages/${layout}/plugin.textproto" VERBATIM)
endforeach()
add_custom_target(cd_beat_machine DEPENDS cd_beat_machine_6x2 cd_beat_machine_4x2)
add_custom_target(cd_beat_machine_package DEPENDS cd_beat_machine_package_6x2 cd_beat_machine_package_4x2)

foreach(tool tests design)
  if(tool STREQUAL "tests")
    set(source tools/Tests.cpp)
  else()
    set(source tools/Design.cpp)
  endif()
  add_executable(cd_beat_machine_${tool} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(cd_beat_machine_${tool} PRIVATE cd_beat_machine_core)
  target_compile_definitions(cd_beat_machine_${tool} PRIVATE CD_BEAT_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

foreach(target cd_beat_machine_core cd_beat_machine_6x2 cd_beat_machine_4x2 cd_beat_machine_tests cd_beat_machine_design)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_beat_machine_core PUBLIC cd_mechanical_art)

add_library(cd_beat_machine_testhost_test_support STATIC EXCLUDE_FROM_ALL tools/TestHost.cpp)
target_link_libraries(cd_beat_machine_testhost_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_beat_machine_testhost_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_beat_machine_testhost_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_beat_machine_testhost_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_beat_machine_tests PRIVATE cd_beat_machine_testhost_test_support)

target_compile_definitions(cd_beat_machine_testhost_test_support PRIVATE CD_BEAT_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
