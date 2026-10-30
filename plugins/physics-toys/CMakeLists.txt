cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyberDesktopPhysicsToys VERSION 1.2.0 LANGUAGES CXX)
  find_package(CyberDesktopSDK 1.4 CONFIG REQUIRED)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_toys_sdk_tool TOYS_LICENSE)
cd_plugin_canvas_license(box2d TOYS_BOX_LICENSE)
cd_plugin_canvas_license(fastnoiselite TOYS_NOISE_LICENSE)
set(TOYS_TOOL cd_toys_sdk_tool)
add_library(cd_physics_toys_core STATIC Model.cpp Renderer.cpp MechanicalRenderer.cpp Instance.cpp)
target_link_libraries(cd_physics_toys_core PUBLIC CyberDesktopSDK::SDK CyberCanvas::Physics2D CyberCanvas::Noise)
target_include_directories(cd_physics_toys_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_physics_toys_core PRIVATE /utf-8 /W4)
set_target_properties(cd_physics_toys_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
foreach(variant chime-2x2 chime-3x2 clock-2x4 clock-1x4 mechanical-chime-2x2 mechanical-clock-2x4 mechanical-clock-1x4)
  set(base_variant "${variant}")
  set(mechanical 0)
  set(style "")
  set(version "1.2.0")
  if(variant MATCHES "^mechanical-")
    string(REPLACE "mechanical-" "" base_variant "${variant}")
    set(mechanical 1)
    set(style ".mechanical")
    set(version "1.0.0")
  endif()
  if(base_variant MATCHES "^chime")
    set(kind "toys::Kind::Chime")
    string(REPLACE "chime-" "" size "${base_variant}")
    set(id "com.cyberdesktop.physics.windchime${style}.${size}")
  else()
    set(kind "toys::Kind::Clock")
    string(REPLACE "clock-" "" size "${base_variant}")
    set(id "com.cyberdesktop.physics.springclock${style}.${size}")
  endif()
  string(REPLACE "-" "_" name "${variant}")
  set(target "cd_toys_${name}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE cd_physics_toys_core)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_TOY_ID="${id}" CD_TOY_KIND=${kind}
    CD_TOY_MECHANICAL=${mechanical} CD_TOY_VERSION="${version}")
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${variant}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  if(mechanical)
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets/mechanical"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/mechanical/${base_variant}" "${input}/assets/mechanical")
  endif()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/physics-toys-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:${TOYS_TOOL}> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${variant}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${variant}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TOYS_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TOYS_BOX_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TOYS_NOISE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
    ${extra}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:${TOYS_TOOL}> pack "${input}" "${CMAKE_BINARY_DIR}/physics-toys-packages/${id}.cdpkg"
    DEPENDS ${target} ${TOYS_TOOL} "${CMAKE_CURRENT_SOURCE_DIR}/packages/${variant}/plugin.textproto" VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_physics_toys_packages DEPENDS ${packages})
add_executable(cd_physics_toys_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
target_compile_definitions(cd_physics_toys_tests PRIVATE CD_TOY_TEST_BINARY_ROOT="${CMAKE_BINARY_DIR}")
add_executable(cd_physics_toys_design EXCLUDE_FROM_ALL tools/Design.cpp)
foreach(target cd_physics_toys_tests cd_physics_toys_design)
  target_link_libraries(${target} PRIVATE cd_physics_toys_core)
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_physics_toys_core PUBLIC cd_mechanical_art)
