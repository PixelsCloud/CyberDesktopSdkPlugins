find_package(nlohmann_json CONFIG REQUIRED)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/RuntimeLibrary.cmake")

add_library(cd_streetlink_rendering STATIC Scene.cpp MatrixFace.cpp)
target_link_libraries(cd_streetlink_rendering PUBLIC cd_mechanical_pixels_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(cd_streetlink_rendering PRIVATE /utf-8 /W4)
set_target_properties(cd_streetlink_rendering PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_streetlink_runtime SHARED Runtime.cpp Physics.cpp)
target_compile_definitions(cd_streetlink_runtime PRIVATE CD_STREETLINK_RUNTIME_BUILD)
target_compile_options(cd_streetlink_runtime PRIVATE /utf-8 /W4)
target_link_libraries(cd_streetlink_runtime PRIVATE cd_streetlink_rendering cd_artwork_rendering cd_beat_machine_core nlohmann_json::nlohmann_json)
add_dependencies(cd_streetlink_runtime cd_mecha_shared_assets)
set_target_properties(cd_streetlink_runtime PROPERTIES OUTPUT_NAME cyberdesktop_streetlink_runtime
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json")
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
foreach(index RANGE 0 ${last})
  string(JSON id GET "${catalog}" ${index} id)
  string(JSON folder GET "${catalog}" ${index} folder)
  foreach(key family sourceId kind rows columns nativeClock nativePhysics)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_streetlink_${folder}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE CyberDesktopSDK::ABI cd_plugin_runtime_loader)
  target_include_directories(${target} PRIVATE "${CD_PLUGIN_SOURCE_ROOT}")
  add_dependencies(${target} cd_streetlink_runtime)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_STREETLINK_ID="${id}")
  if(nativeClock OR nativePhysics)
    target_compile_definitions(${target} PRIVATE CD_STREETLINK_NATIVE_CLOCK=1
      CD_STREETLINK_RUNTIME_NAME=L"cyberdesktop_streetlink_runtime.dll" CD_STREETLINK_FACTORY="cd_streetlink_create")
  else()
    add_dependencies(${target} cd_mecha_runtime)
    target_compile_definitions(${target} PRIVATE CD_STREETLINK_NATIVE_CLOCK=0
      CD_STREETLINK_RUNTIME_NAME=L"cyberdesktop_mecha_runtime.dll" CD_STREETLINK_FACTORY="cd_artwork_create"
      CD_STREETLINK_FAMILY="${family}" CD_STREETLINK_SOURCE="${sourceId}" CD_STREETLINK_KIND=${kind}
      CD_STREETLINK_ROWS=${rows} CD_STREETLINK_COLUMNS=${columns})
  endif()
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
  set(package "${CMAKE_BINARY_DIR}/plugins/${id}")
  set(extra)
  if(family STREQUAL "weather")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/assets/icons" "${package}/assets/icons"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses" "${package}/licenses")
  endif()
  cd_asset_target(${target}_package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${package}/licenses" "${CMAKE_BINARY_DIR}/streetlink-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}" "${package}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${package}/preview.png"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${package}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${package}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${package}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/assets/fonts/Aeris/orbitron-OFL.txt" "${package}/licenses/Orbitron-OFL.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/assets/fonts/Aeris/chakrapetch-OFL.txt" "${package}/licenses/Chakra-Petch-OFL.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/plugins/koi-suite/licenses/animal-proc-anim-MIT.txt" "${package}/licenses/animal-proc-anim-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/lunar-suite/licenses" "${package}/licenses"
    ${extra}
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${package}" "${CMAKE_BINARY_DIR}/streetlink-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_streetlink_packages DEPENDS ${packages})
cd_asset_target(cd_streetlink_shared_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/shared" "${CMAKE_BINARY_DIR}/assets/streetlink"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/assets/fonts/Streetlink" "${CMAKE_BINARY_DIR}/assets/fonts/Streetlink")
add_dependencies(cd_streetlink_runtime cd_streetlink_shared_assets)

add_executable(cd_streetlink_verify EXCLUDE_FROM_ALL tools/Verify.cpp)
add_dependencies(cd_streetlink_verify cd_streetlink_shared_assets)
target_link_libraries(cd_streetlink_verify PRIVATE cd_streetlink_rendering cd_mechanical_pixels_testhost_test_support
  nlohmann_json::nlohmann_json)
target_compile_options(cd_streetlink_verify PRIVATE /utf-8 /W4)
set_target_properties(cd_streetlink_verify PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

set_property(TARGET cd_streetlink_shared_assets PROPERTY CD_DISTRIBUTION_ASSETS "assets/streetlink;assets/fonts/Streetlink")
