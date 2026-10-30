cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/RuntimeLibrary.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
add_library(cd_deep_whale_core STATIC Model.cpp Renderer.cpp ClockInstance.cpp)
target_link_libraries(cd_deep_whale_core PUBLIC cd_mechanical_pixels_core)
add_library(cd_deep_whale_spring_core STATIC SpringClock.cpp SpringInstance.cpp)
target_link_libraries(cd_deep_whale_spring_core PUBLIC cd_physics_toys_core cd_mechanical_pixels_core)
add_library(cd_deep_whale_marble_core STATIC MarbleClock.cpp MarbleInstance.cpp)
target_link_libraries(cd_deep_whale_marble_core PUBLIC cd_marble_clock_core cd_mechanical_pixels_core)
foreach(core cd_deep_whale_core cd_deep_whale_spring_core cd_deep_whale_marble_core)
  target_include_directories(${core} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  target_compile_options(${core} PRIVATE /utf-8 /W4)
  set_target_properties(${core} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
find_package(nlohmann_json CONFIG REQUIRED)
cd_asset_target(cd_deep_whale_shared_fonts
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets/fonts/DeepWhale"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CD_PLUGIN_SOURCE_ROOT}/assets/fonts/DeepWhale/LXGWWenKaiGBScreen.ttf"
    "${CD_PLUGIN_SOURCE_ROOT}/assets/fonts/DeepWhale/LXGWWenKai-OFL.txt"
    "${CMAKE_BINARY_DIR}/assets/fonts/DeepWhale")
add_library(cd_deep_whale_runtime SHARED Skin.cpp Runtime.cpp PrecisionRenderer.cpp CalendarRenderers.cpp
  controllers/precision/Instance.cpp controllers/calendar/Instance.cpp controllers/lunar/Instance.cpp
  SpectrumInstance.cpp SpectrumRenderer.cpp BasicClockRenderer.cpp BasicClockInstance.cpp PlayerRenderer.cpp PlayerInstance.cpp
  PondRenderer.cpp PondInstance.cpp AmbienceRenderer.cpp AmbienceInstance.cpp Motion.cpp
  BrickRenderer.cpp BrickInstance.cpp HourglassRenderer.cpp SignRenderer.cpp
  controllers/sign/Instance.cpp controllers/hourglass/Instance.cpp
  UtilityRenderers.cpp WeatherRenderer.cpp controllers/weather/Instance.cpp controllers/air/Instance.cpp
  controllers/monitor/Instance.cpp controllers/productivity/Instance.cpp controllers/widget/Instance.cpp controllers/matrix/Instance.cpp)
add_dependencies(cd_deep_whale_runtime cd_deep_whale_shared_fonts)
target_compile_definitions(cd_deep_whale_runtime PRIVATE CD_DEEP_RUNTIME_BUILD)
target_link_libraries(cd_deep_whale_runtime PRIVATE cd_physics_toys_core cd_koi_core cd_ambience_core cd_beat_machine_core cd_brick_clock_core cd_mechanical_sign_core cd_mechanical_hourglass_core cd_clock_core cd_precision_clocks_core cd_mechanical_calendar_core cd_lunar_core cd_weather_core cd_air_core cd_monitor_core cd_productivity_core cd_widget_core cd_mechanical_pixels_core nlohmann_json::nlohmann_json)
target_compile_options(cd_deep_whale_runtime PRIVATE /utf-8 /W4)
set_target_properties(cd_deep_whale_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" OUTPUT_NAME cyberdesktop_deep_whale_runtime
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS catalog.json)
set(packages)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
foreach(index RANGE 0 ${last})
  foreach(key id folder family)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_deep_whale_${folder}")
  if(family STREQUAL "spring")
    set(source SpringPlugin.cpp)
    set(core cd_deep_whale_spring_core)
  elseif(family STREQUAL "marble")
    set(source MarblePlugin.cpp)
    set(core cd_deep_whale_marble_core)
  elseif(NOT family STREQUAL "clock")
    set(source RuntimePlugin.cpp)
    set(core CyberDesktopSDK::ABI cd_plugin_runtime_loader)
  else()
    set(source Plugin.cpp)
    set(core cd_deep_whale_core)
  endif()
  add_library(${target} MODULE ${source})
  target_link_libraries(${target} PRIVATE ${core})
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_DEEP_WHALE_ID="${id}")
  if(source STREQUAL "RuntimePlugin.cpp")
    foreach(key sourceId kind rows columns)
      string(JSON ${key} GET "${catalog}" ${index} ${key})
    endforeach()
    target_compile_definitions(${target} PRIVATE CD_DEEP_ID="${id}" CD_DEEP_FAMILY="${family}"
      CD_DEEP_SOURCE="${sourceId}" CD_DEEP_KIND=${kind} CD_DEEP_ROWS=${rows} CD_DEEP_COLUMNS=${columns})
    add_dependencies(${target} cd_deep_whale_runtime)
  endif()
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  if(family STREQUAL "spring" OR family STREQUAL "marble")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/BreeSerif-OFL.txt" "${input}/licenses/BreeSerif-OFL.txt")
  endif()
  if(source STREQUAL "RuntimePlugin.cpp")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E rm -f "${input}/assets/LXGWWenKaiGBScreen.ttf" "$<TARGET_FILE_DIR:${target}>/assets/LXGWWenKaiGBScreen.ttf"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${nlohmann_json_DIR}/../nlohmann-json/copyright" "${input}/licenses/Nlohmann-JSON-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/BreeSerif-OFL.txt" "${input}/licenses/BreeSerif-OFL.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/LXGWWenKai-OFL.txt" "${input}/licenses/LXGWWenKai-OFL.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/lunar-suite/licenses" "${input}/licenses"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/koi-suite/licenses" "${input}/licenses"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC-MIT.txt")
  endif()
  if(family STREQUAL "weather")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Meteocons-MIT.txt" "${input}/licenses/Meteocons-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Open-Meteo-Attribution.txt" "${input}/licenses/Open-Meteo-Attribution.txt")
  endif()
  set(runtime_assets)
  file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}/*")
  foreach(asset IN LISTS asset_files)
    file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}" "${asset}")
    get_filename_component(directory "${relative}" DIRECTORY)
    list(APPEND runtime_assets
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets/${directory}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${input}/assets/${relative}" "$<TARGET_FILE_DIR:${target}>/assets/${relative}")
  endforeach()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses" "${CMAKE_BINARY_DIR}/deep-whale-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    ${extra}
    ${runtime_assets}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}/licenses" "$<TARGET_FILE_DIR:${target}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${input}/descriptor.pb" "${input}/preview.png" "${input}/README.txt" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/deep-whale-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_deep_whale_packages DEPENDS ${packages})

target_sources(cd_deep_whale_runtime PRIVATE BasicClock.cpp BeatScene.cpp SpectrumSprites.cpp)


set_property(TARGET cd_deep_whale_shared_fonts PROPERTY CD_DISTRIBUTION_ASSETS "assets/fonts/DeepWhale")
