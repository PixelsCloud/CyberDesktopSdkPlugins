cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopMarbleClock VERSION 1.2.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_marble_sdk_tool MARBLE_LICENSE)
cd_plugin_canvas_license(box2d MARBLE_BOX_LICENSE)
set(MARBLE_TOOL cd_marble_sdk_tool)

add_library(cd_marble_clock_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_include_directories(cd_marble_clock_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_marble_clock_core PUBLIC CyberDesktopSDK::SDK CyberCanvas::Physics2D)
add_custom_target(cd_marble_clock_package)
add_custom_target(cd_marble_clock_assets)
set(marble_targets cd_marble_clock_core)
foreach(layout 6x2 4x2)
  set(target "cd_marble_clock_${layout}")
  set(id "com.cyberdesktop.clock.marble.${layout}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_marble_clock_core)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MARBLE_ID="${id}")
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/link/${layout}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}")
  list(APPEND marble_targets ${target})
  cd_asset_target(cd_marble_clock_assets_${layout}
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "-DASSET_DIRECTORY=$<TARGET_FILE_DIR:${target}>/assets" "-DBUILD_ROOT=${CMAKE_BINARY_DIR}" "-DLAYOUT=${layout}" "-DKIND=clock"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../mechanical-instruments/StagePhysicsAssets.cmake"
    VERBATIM)
  add_dependencies(cd_marble_clock_assets cd_marble_clock_assets_${layout})
  add_dependencies(${target} cd_marble_clock_assets)
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  cd_asset_target(cd_marble_clock_package_${layout}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/marble-clock-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:${MARBLE_TOOL}> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${layout}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${layout}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MARBLE_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MARBLE_BOX_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "-DASSET_DIRECTORY=${input}/assets" "-DBUILD_ROOT=${CMAKE_BINARY_DIR}" "-DLAYOUT=${layout}" "-DKIND=clock"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../mechanical-instruments/StagePhysicsAssets.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:${MARBLE_TOOL}> pack "${input}" "${CMAKE_BINARY_DIR}/marble-clock-packages/${id}.cdpkg"
    DEPENDS ${target} ${MARBLE_TOOL} "${CMAKE_CURRENT_SOURCE_DIR}/packages/${layout}/plugin.textproto" VERBATIM)
  add_dependencies(cd_marble_clock_package cd_marble_clock_package_${layout})
endforeach()

foreach(tool tests design benchmark)
  if(tool STREQUAL "tests")
    set(source tools/Tests.cpp)
  elseif(tool STREQUAL "design")
    set(source tools/Design.cpp)
  else()
    set(source tools/Benchmark.cpp)
  endif()
  add_executable(cd_marble_clock_${tool} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(cd_marble_clock_${tool} PRIVATE cd_marble_clock_core)
  target_compile_definitions(cd_marble_clock_${tool} PRIVATE CD_MARBLE_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  if(tool STREQUAL "tests")
    target_compile_definitions(cd_marble_clock_tests PRIVATE CD_MARBLE_TEST_OUTPUT="${CMAKE_BINARY_DIR}/Testing/marble-clock")
  endif()
  list(APPEND marble_targets cd_marble_clock_${tool})
endforeach()
foreach(target IN LISTS marble_targets)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_marble_clock_core PUBLIC cd_mechanical_art)
