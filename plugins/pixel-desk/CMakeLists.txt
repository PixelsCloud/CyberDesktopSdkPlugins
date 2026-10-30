cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")

add_library(cd_pixel_desk_core STATIC Model.cpp Renderer.cpp ClockInstance.cpp)
target_link_libraries(cd_pixel_desk_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_spring_core STATIC SpringClock.cpp)
target_link_libraries(cd_pixel_desk_spring_core PUBLIC cd_pixel_desk_toys_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_spring_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_spring_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_spring_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Pixel geometry is a profile of the single shared physics model.
add_library(cd_pixel_desk_marble_core STATIC MarbleClock.cpp)

target_link_libraries(cd_pixel_desk_marble_core PUBLIC cd_marble_clock_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_marble_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_marble_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_marble_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_precision_core STATIC PrecisionClock.cpp)
target_link_libraries(cd_pixel_desk_precision_core PUBLIC cd_precision_clocks_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_precision_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_precision_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_precision_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_calendar_core STATIC CalendarRenderer.cpp CalendarInstance.cpp)
target_link_libraries(cd_pixel_desk_calendar_core PUBLIC cd_mechanical_calendar_core)
target_include_directories(cd_pixel_desk_calendar_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_calendar_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_calendar_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_lunar_core STATIC LunarInstance.cpp LunarRenderer.cpp)
target_link_libraries(cd_pixel_desk_lunar_core PUBLIC cd_lunar_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_lunar_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_lunar_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_lunar_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_weather_core STATIC WeatherInstance.cpp WeatherRenderer.cpp)
target_link_libraries(cd_pixel_desk_weather_core PUBLIC cd_weather_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_weather_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_weather_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_weather_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_air_core STATIC AirInstance.cpp AirRenderer.cpp)
target_link_libraries(cd_pixel_desk_air_core PUBLIC cd_air_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_air_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_air_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_air_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_monitor_core STATIC MonitorRenderer.cpp)
target_link_libraries(cd_pixel_desk_monitor_core PUBLIC cd_monitor_core cd_mechanical_pixels_core CyberCanvas::Motion
  PRIVATE d2d1 dwrite windowscodecs ole32 user32)
target_include_directories(cd_pixel_desk_monitor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_monitor_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_monitor_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

cd_plugin_json_license(CD_PIXEL_JSON_LICENSE)
include("${CMAKE_CURRENT_LIST_DIR}/../common/ProductivityVolume.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../common/ContentText.cmake")
add_library(cd_pixel_desk_productivity_core STATIC ProductivityRenderer.cpp DiskRenderer.cpp TimerRenderer.cpp CountdownRenderer.cpp
)
target_link_libraries(cd_pixel_desk_productivity_core PUBLIC cd_productivity_core cd_pixel_desk_basic_clock_core cd_mechanical_pixels_core CyberCanvas::Motion
  PRIVATE cd_productivity_volume nlohmann_json::nlohmann_json cd_content_text d2d1 dwrite windowscodecs ole32 shell32 user32)
target_include_directories(cd_pixel_desk_productivity_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_productivity_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_productivity_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_input_core STATIC InputInstance.cpp InputRenderer.cpp)
target_link_libraries(cd_pixel_desk_input_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_input_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_input_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_input_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_album_core STATIC AlbumInstance.cpp AlbumRenderer.cpp)
target_link_libraries(cd_pixel_desk_album_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_album_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_album_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_album_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_player_core STATIC PlayerInstance.cpp PlayerRenderer.cpp)
target_link_libraries(cd_pixel_desk_player_core PUBLIC cd_widget_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_player_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_player_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_player_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_matrix_core STATIC MatrixInstance.cpp MatrixRenderer.cpp ContinuousSpectrum.cpp)
target_link_libraries(cd_pixel_desk_matrix_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_matrix_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_matrix_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_matrix_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Geometry selects the shared solver; no generated implementation or duplicate audio model.
add_library(cd_pixel_desk_beat_core STATIC BeatRenderer.cpp BeatInstance.cpp)
target_link_libraries(cd_pixel_desk_beat_core PUBLIC cd_beat_machine_core cd_mechanical_pixels_core)

target_include_directories(cd_pixel_desk_beat_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../.." "${CMAKE_CURRENT_SOURCE_DIR}/../beat-machine")
target_compile_options(cd_pixel_desk_beat_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_beat_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# All physics is compiled once; the Pixel target supplies only its profile.
add_library(cd_pixel_desk_toys_core STATIC PixelToysModel.cpp)
target_link_libraries(cd_pixel_desk_toys_core PUBLIC cd_physics_toys_core)
target_include_directories(cd_pixel_desk_toys_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_toys_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_toys_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_chime_core STATIC ChimeRenderer.cpp ChimeInstance.cpp)
target_include_directories(cd_pixel_desk_chime_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../physics-toys")
target_link_libraries(cd_pixel_desk_chime_core PUBLIC cd_pixel_desk_toys_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_chime_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_chime_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_chime_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_sign_core STATIC SignRenderer.cpp SignLayout.cpp ../mechanical-sign/Model.cpp SignInstance.cpp)
target_link_libraries(cd_pixel_desk_sign_core PUBLIC cd_mechanical_pixels_core icu)
target_include_directories(cd_pixel_desk_sign_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_sign_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_sign_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_hourglass_core STATIC HourglassLayout.cpp HourglassRenderer.cpp HourglassInstance.cpp)
target_link_libraries(cd_pixel_desk_hourglass_core PUBLIC cd_hourglass_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_hourglass_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_hourglass_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_hourglass_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_basic_clock_core STATIC BasicClock.cpp)
target_link_libraries(cd_pixel_desk_basic_clock_core PUBLIC cd_clock_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_basic_clock_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_basic_clock_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_basic_clock_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# The plaque aperture configures the shared rigid-body enclosure.
add_library(cd_pixel_desk_brick_core STATIC PixelBrick.cpp BrickInstance.cpp)

target_link_libraries(cd_pixel_desk_brick_core PUBLIC cd_pixel_desk_basic_clock_core cd_brick_clock_core)
target_include_directories(cd_pixel_desk_brick_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../.." PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../brick-clock")
target_compile_options(cd_pixel_desk_brick_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_brick_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_pond_core STATIC PondRenderer.cpp PondInstance.cpp)
target_link_libraries(cd_pixel_desk_pond_core PUBLIC cd_pixel_desk_basic_clock_core cd_koi_core)
target_include_directories(cd_pixel_desk_pond_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_pond_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_pond_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Rendering is theme-owned; controller, model, persistence and audio grouping are shared.
add_library(cd_pixel_desk_ambience_core STATIC AmbienceRenderer.cpp)

target_include_directories(cd_pixel_desk_ambience_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_pixel_desk_ambience_core PUBLIC cd_ambience_core cd_pixel_desk_basic_clock_core CyberCanvas::Noise)
target_compile_options(cd_pixel_desk_ambience_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_ambience_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
foreach(index RANGE 0 ${last})
  foreach(key id folder art family)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_pixel_desk_${folder}")
  set(extra)
  set(art_dependencies)
  if(family STREQUAL "spring")
    add_library(${target} MODULE SpringPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_spring_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "chime")
    add_library(${target} MODULE ChimePlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_chime_core)
  elseif(family STREQUAL "marble")
    add_library(${target} MODULE MarblePlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_marble_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "hourglass")
    add_library(${target} MODULE HourglassPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_hourglass_core)
  elseif(family STREQUAL "sign")
    add_library(${target} MODULE SignPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_sign_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "beat")
    add_library(${target} MODULE BeatPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_beat_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "matrix" OR family STREQUAL "spectrum")
    add_library(${target} MODULE MatrixPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_matrix_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "player")
    add_library(${target} MODULE PlayerPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_player_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "album")
    add_library(${target} MODULE AlbumPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_album_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "input")
    add_library(${target} MODULE InputPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_input_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "productivity" OR family STREQUAL "disk" OR family STREQUAL "timer" OR family STREQUAL "countdown")
    add_library(${target} MODULE ProductivityPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_productivity_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "monitor")
    add_library(${target} MODULE MonitorPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_monitor_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "air")
    add_library(${target} MODULE AirPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_air_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "weather")
    add_library(${target} MODULE WeatherPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_weather_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "lunar")
    add_library(${target} MODULE LunarPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_lunar_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "calendar")
    add_library(${target} MODULE CalendarPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_calendar_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "plant" OR family STREQUAL "noise")
    add_library(${target} MODULE AmbiencePlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_ambience_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "koi")
    add_library(${target} MODULE PondPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_pond_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "brick")
    add_library(${target} MODULE BrickPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_brick_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "basic-clock")
    add_library(${target} MODULE BasicClockPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_basic_clock_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "precision")
    add_library(${target} MODULE PrecisionPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_precision_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  else()
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  endif()
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" descriptor)
  string(REGEX MATCH "version: \"([^\"]+)\"" version_match "${descriptor}")
  set(version "${CMAKE_MATCH_1}")
  if(NOT version)
    message(FATAL_ERROR "Pixel Desk descriptor has no version: ${folder}")
  endif()
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_PIXEL_DESK_ID="${id}" CD_PIXEL_DESK_VERSION="${version}")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  if(family STREQUAL "spring" OR family STREQUAL "chime")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt")
  elseif(family STREQUAL "hourglass")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
  else()
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/provenance.json" "${input}/assets")
  endif()
  if(family STREQUAL "spring")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/provenance.json" "${input}/assets")
  endif()
  string(JSON runtime_assets ERROR_VARIABLE no_runtime_assets GET "${catalog}" ${index} runtimeAssets)
  if(no_runtime_assets STREQUAL "NOTFOUND")
    string(JSON asset_count LENGTH "${runtime_assets}")
    math(EXPR asset_last "${asset_count} - 1")
    foreach(asset_index RANGE 0 ${asset_last})
      string(JSON runtime_name MEMBER "${runtime_assets}" ${asset_index})
      string(JSON source_asset GET "${runtime_assets}" "${runtime_name}")
      list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${source_asset}")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${source_asset}" "${input}/assets/${runtime_name}")
    endforeach()
  endif()
  if(family STREQUAL "marble" OR family STREQUAL "beat" OR family STREQUAL "brick")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
  endif()
  if(family STREQUAL "plant" OR family STREQUAL "noise")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC.txt")
  endif()
  if(family STREQUAL "koi")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/koi-suite/licenses/animal-proc-anim-MIT.txt" "${input}/licenses")
  endif()
  if(family STREQUAL "precision" OR family STREQUAL "lunar")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/third_party/sxtwl/LICENSE"
      "${input}/licenses/sxtwl-BSD-3-Clause.txt")
  endif()
  if(family STREQUAL "calendar" OR family STREQUAL "lunar" OR family STREQUAL "weather" OR family STREQUAL "air" OR family STREQUAL "input" OR family STREQUAL "album" OR family STREQUAL "player" OR family STREQUAL "timer")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC.txt")
  endif()
  if(family STREQUAL "marble" OR family STREQUAL "beat" OR family STREQUAL "chime" OR family STREQUAL "spring")
    list(APPEND art_dependencies "${CD_PIXEL_JSON_LICENSE}")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PIXEL_JSON_LICENSE}" "${input}/licenses/nlohmann-json-MIT.txt")
  endif()
  if(family STREQUAL "weather")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/nlohmann-json-MIT.txt"
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Open-Meteo-Attribution.txt" "${input}/licenses")
  endif()
  if(family STREQUAL "air")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/air-quality-suite/licenses/nlohmann-json-MIT.txt"
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/air-quality-suite/licenses/WAQI-Attribution.txt" "${input}/licenses")
  endif()
  if(family STREQUAL "productivity" OR family STREQUAL "disk" OR family STREQUAL "timer" OR family STREQUAL "countdown")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/productivity-suite/licenses" "${input}/licenses")
  endif()
  if(folder STREQUAL "weather-4x2")
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/weather-scene/city.png")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/weather-scene/city.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/weather-scene/city.provenance.json" "${input}/assets")
  endif()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses"
      "${CMAKE_BINARY_DIR}/pixel-desk-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}"
      "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    ${extra}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/pixel-desk-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto"
      ${art_dependencies} VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_pixel_desk_packages DEPENDS ${packages})


# Shared case artwork is independent of any individual widget model.
add_library(cd_pixel_desk_artwork STATIC PixelParts.cpp PixelLayout.cpp PixelImages.cpp PixelDigits.cpp PixelPanels.cpp PixelCase.cpp)
target_link_libraries(cd_pixel_desk_artwork PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_pixel_desk_artwork PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_artwork PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_artwork PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
foreach(target cd_pixel_desk_core cd_pixel_desk_spring_core cd_pixel_desk_marble_core cd_pixel_desk_precision_core cd_pixel_desk_calendar_core cd_pixel_desk_lunar_core cd_pixel_desk_weather_core cd_pixel_desk_air_core cd_pixel_desk_monitor_core cd_pixel_desk_productivity_core cd_pixel_desk_input_core cd_pixel_desk_album_core cd_pixel_desk_player_core cd_pixel_desk_matrix_core cd_pixel_desk_beat_core cd_pixel_desk_toys_core cd_pixel_desk_chime_core cd_pixel_desk_sign_core cd_pixel_desk_hourglass_core cd_pixel_desk_basic_clock_core cd_pixel_desk_brick_core cd_pixel_desk_pond_core cd_pixel_desk_ambience_core)
  target_link_libraries(${target} PUBLIC cd_pixel_desk_artwork)
endforeach()

target_sources(cd_pixel_desk_weather_core PRIVATE WeatherArt.cpp)
target_sources(cd_pixel_desk_sign_core PRIVATE SignGlyphs.cpp)
target_sources(cd_pixel_desk_chime_core PRIVATE PixelChime.cpp)
target_sources(cd_pixel_desk_hourglass_core PRIVATE PixelHourglass.cpp)
target_sources(cd_pixel_desk_beat_core PRIVATE PixelBeat.cpp BeatArtwork.cpp)
