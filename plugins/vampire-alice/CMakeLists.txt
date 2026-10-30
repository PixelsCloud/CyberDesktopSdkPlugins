cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/RuntimeLibrary.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB controller_sources CONFIGURE_DEPENDS "controllers/*/Instance.cpp")
add_library(cd_alice_runtime SHARED Skin.cpp Renderers.cpp CalendarsWeather.cpp PhysicsRenderers.cpp Runtime.cpp ToysModel.cpp MarbleModel.cpp BeatModel.cpp ${controller_sources})
target_compile_definitions(cd_alice_runtime PRIVATE CD_ALICE_RUNTIME_BUILD)
target_compile_options(cd_alice_runtime PRIVATE /utf-8 /W4)
target_link_libraries(cd_alice_runtime PRIVATE
  cd_mechanical_pixels_core cd_precision_clocks_core cd_physics_toys_core cd_marble_clock_core cd_beat_machine_core
  cd_mechanical_calendar_core cd_mechanical_hourglass_core cd_mechanical_sign_core
  cd_widget_core cd_weather_core cd_air_core cd_lunar_core cd_productivity_core cd_monitor_core
  nlohmann_json::nlohmann_json)
set_target_properties(cd_alice_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" OUTPUT_NAME cyberdesktop_alice_runtime
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
set(modules)
foreach(index RANGE 0 ${last})
  foreach(key id folder family sourceId kind rows columns)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_alice_${folder}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE CyberDesktopSDK::ABI cd_plugin_runtime_loader)
  add_dependencies(${target} cd_alice_runtime)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_ALICE_ID="${id}" CD_ALICE_FAMILY="${family}"
    CD_ALICE_SOURCE="${sourceId}" CD_ALICE_KIND=${kind} CD_ALICE_ROWS=${rows} CD_ALICE_COLUMNS=${columns})
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/matte.png")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/matte.png" "${input}/assets/matte.png")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/hardware.png")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/hardware.png" "${input}/assets/hardware.png")
  endif()
  if(family STREQUAL "weather")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/illustrations.png" "${input}/assets/illustrations.png"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/assets/icons" "${input}/assets/icons"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Meteocons-MIT.txt" "${input}/licenses/Meteocons-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Open-Meteo-Attribution.txt" "${input}/licenses/Open-Meteo-Attribution.txt")
  endif()
  cd_asset_target(${target}_package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses" "${CMAKE_BINARY_DIR}/alice-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/body.png" "${input}/assets/body.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/skin.json" "${input}/assets/skin.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${nlohmann_json_DIR}/../nlohmann-json/copyright" "${input}/licenses/Nlohmann-JSON-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/lunar-suite/licenses" "${input}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
    ${extra}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/alice-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool VERBATIM)
  list(APPEND packages ${target}_package)
  list(APPEND modules ${target})
endforeach()
add_custom_target(cd_alice_modules DEPENDS ${modules})
add_custom_target(cd_alice_packages DEPENDS ${packages})
