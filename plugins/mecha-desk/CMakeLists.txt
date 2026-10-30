cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/RuntimeLibrary.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
find_package(nlohmann_json CONFIG REQUIRED)

include("${CMAKE_CURRENT_SOURCE_DIR}/../common/ArtworkRendering.cmake")

file(GLOB controller_sources CONFIGURE_DEPENDS "controllers/*/Instance.cpp")
add_library(cd_mecha_runtime SHARED Runtime.cpp PrecisionRenderer.cpp CalendarRenderers.cpp SpectrumInstance.cpp SpectrumRenderer.cpp BasicClockRenderer.cpp BasicClockInstance.cpp PlayerRenderer.cpp PlayerInstance.cpp PondRenderer.cpp PondInstance.cpp AmbienceRenderer.cpp AmbienceInstance.cpp BrickRenderer.cpp BrickInstance.cpp HourglassRenderer.cpp SignRenderer.cpp UtilityRenderers.cpp WeatherRenderer.cpp SpringRenderer.cpp ToysModel.cpp Armour.cpp ${controller_sources})
target_compile_definitions(cd_mecha_runtime PRIVATE CD_MECHA_RUNTIME_BUILD)
target_compile_options(cd_mecha_runtime PRIVATE /utf-8 /W4)
target_link_libraries(cd_mecha_runtime PRIVATE
  cd_artwork_rendering
  cd_mechanical_pixels_core cd_precision_clocks_core cd_physics_toys_core cd_marble_clock_core cd_beat_machine_core
  cd_mechanical_calendar_core cd_mechanical_hourglass_core cd_mechanical_sign_core
  cd_clock_core cd_koi_core cd_ambience_core cd_brick_clock_core cd_widget_core cd_weather_core cd_air_core cd_lunar_core cd_productivity_core cd_monitor_core
  nlohmann_json::nlohmann_json)
set_target_properties(cd_mecha_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" OUTPUT_NAME cyberdesktop_mecha_runtime
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json")
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
set(modules)
foreach(index RANGE 0 ${last})
  foreach(key id folder family sourceId kind rows columns)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_mecha_${folder}")
  add_library(${target} MODULE RuntimePlugin.cpp)
  target_link_libraries(${target} PRIVATE CyberDesktopSDK::ABI cd_plugin_runtime_loader)
  add_dependencies(${target} cd_mecha_runtime)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MECHA_ID="${id}" CD_MECHA_FAMILY="${family}"
    CD_MECHA_SOURCE="${sourceId}" CD_MECHA_KIND=${kind} CD_MECHA_ROWS=${rows} CD_MECHA_COLUMNS=${columns})
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  if(family STREQUAL "pond")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/koi-suite/licenses/animal-proc-anim-MIT.txt"
      "${input}/licenses/animal-proc-anim-MIT.txt")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/plate.png")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/plate.png" "${input}/assets/plate.png")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/hardware.png")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/hardware.png" "${input}/assets/hardware.png")
  endif()
  if(family STREQUAL "weather")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/assets/icons" "${input}/assets/icons"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Meteocons-MIT.txt" "${input}/licenses/Meteocons-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Open-Meteo-Attribution.txt" "${input}/licenses/Open-Meteo-Attribution.txt")
  endif()
  cd_asset_target(${target}_package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses" "${CMAKE_BINARY_DIR}/mecha-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/skin.json" "${input}/assets/skin.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}" "${input}/assets"
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
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mecha-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool VERBATIM)
  list(APPEND packages ${target}_package)
  list(APPEND modules ${target})
endforeach()
add_custom_target(cd_mecha_modules DEPENDS ${modules})
add_custom_target(cd_mecha_packages DEPENDS ${packages})


cd_asset_target(cd_mecha_shared_assets ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/shared" "${CMAKE_BINARY_DIR}/assets/mecha"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/assets/fonts/Aeris" "${CMAKE_BINARY_DIR}/assets/fonts/Aeris")
add_dependencies(cd_mecha_runtime cd_mecha_shared_assets)



target_sources(cd_mecha_runtime PRIVATE BasicClock.cpp PlayerRegions.cpp)



set_property(TARGET cd_mecha_shared_assets PROPERTY CD_DISTRIBUTION_ASSETS "assets/mecha;assets/fonts/Aeris")
