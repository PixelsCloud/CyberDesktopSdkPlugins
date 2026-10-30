cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
# Each style/footprint is a separately compiled module with a distinct immutable
# identity. Only implementation libraries and instrument parts are shared.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
set(CD_MECHANICAL_YOGA_LICENSE "${CD_PLUGIN_YOGA_LICENSE}")
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
set(packages)
foreach(index RANGE 0 ${last})
  foreach(key id folder family suite kind rows columns version)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  if(family STREQUAL "monitor")
    set(core cd_monitor_core)
    set(core_index 0)
  elseif(family STREQUAL "weather")
    set(core cd_weather_core)
    set(core_index 2)
  elseif(family STREQUAL "player" OR family STREQUAL "album" OR family STREQUAL "spectrum" OR family STREQUAL "radial" OR family STREQUAL "input")
    set(core cd_widget_core)
    set(core_index 3)
  elseif(family STREQUAL "air")
    set(core cd_air_core)
    set(core_index 4)
  elseif(family STREQUAL "lunar")
    set(core cd_lunar_core)
    set(core_index 5)
  else()
    set(core cd_productivity_core)
    set(core_index 1)
  endif()
  string(REPLACE "-" "_" target "cd_mechanical_${folder}")
  add_library(${target} MODULE Plugin.cpp)
  target_link_libraries(${target} PRIVATE ${core})
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MECHANICAL_CORE=${core_index}
    CD_MECHANICAL_ID="${id}" CD_MECHANICAL_VERSION="${version}" CD_MECHANICAL_KIND=${kind}
    CD_MECHANICAL_ROWS=${rows} CD_MECHANICAL_COLUMNS=${columns})
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  set(art_family "${family}")
  if(family STREQUAL "album" OR (family STREQUAL "player" AND NOT (rows EQUAL 2 AND columns EQUAL 4)))
    set(art_family "${folder}")
  endif()
  if(family STREQUAL "monitor" OR family STREQUAL "air" OR family STREQUAL "spectrum" OR family STREQUAL "radial" OR
     family STREQUAL "input" OR family STREQUAL "lunar" OR
     (family STREQUAL "network" AND columns EQUAL 4))
    set(art_family "${folder}")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${suite}/licenses")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/../${suite}/licenses" "${input}/licenses")
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${suite}/assets")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/../${suite}/assets" "${input}/assets")
  endif()
  if(family STREQUAL "monitor" OR family STREQUAL "air" OR family STREQUAL "spectrum" OR family STREQUAL "radial" OR
     family STREQUAL "input" OR family STREQUAL "lunar" OR
     (family STREQUAL "network" AND columns EQUAL 4) OR
     (rows EQUAL 2 AND columns EQUAL 2 AND (family STREQUAL "timer" OR family STREQUAL "disk")) OR
     family STREQUAL "player" OR family STREQUAL "album" OR (rows EQUAL 2 AND columns EQUAL 4 AND family STREQUAL "weather"))
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E remove_directory "${input}/assets/mechanical"
      COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${target}>/assets/mechanical"
      COMMAND ${CMAKE_COMMAND} -E rm -f "${input}/licenses/Lucide-ISC.txt" "$<TARGET_FILE_DIR:${target}>/licenses/Lucide-ISC.txt"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets/mechanical"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art_family}/body.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art_family}/provenance.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art_family}/NOTICE.txt"
        "${input}/assets/mechanical")
    if(family STREQUAL "player")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/player/arm.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/player/vinyl.png" "${input}/assets/mechanical")
    endif()
    if(family STREQUAL "album")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art_family}/controls.png" "${input}/assets/mechanical")
    endif()
    if(family STREQUAL "spectrum" OR family STREQUAL "radial")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art_family}/rod.png" "${input}/assets/mechanical")
    endif()
    if(family STREQUAL "spectrum")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art_family}/ball.png" "${input}/assets/mechanical")
    endif()
    if(family STREQUAL "lunar")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/cyberglass/resources/lucide/LICENSE.txt"
        "${input}/licenses/Lucide-ISC-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../lunar-suite/provenance.json" "${input}/lunar-provenance.json")
    endif()
  endif()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/mechanical-instruments-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MECHANICAL_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    ${extra}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mechanical-instruments-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_mechanical_instruments_packages DEPENDS ${packages})
add_executable(cd_mechanical_panels_design EXCLUDE_FROM_ALL tools/PanelsDesign.cpp)
target_link_libraries(cd_mechanical_panels_design PRIVATE cd_monitor_core cd_productivity_core cd_mechanical_sign_core)
target_compile_options(cd_mechanical_panels_design PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_panels_design PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(TARGET cd_player_plugin_tests)
  add_dependencies(cd_player_plugin_tests cd_mechanical_player_1x4_package cd_mechanical_player_2x2_package cd_mechanical_player_2x4_package cd_mechanical_album_2x4_package)
endif()
add_executable(cd_mechanical_air_toys_design EXCLUDE_FROM_ALL tools/AirToysDesign.cpp)
target_link_libraries(cd_mechanical_air_toys_design PRIVATE cd_air_core cd_physics_toys_core)
target_compile_options(cd_mechanical_air_toys_design PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_air_toys_design PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(cd_mechanical_spectrum_design EXCLUDE_FROM_ALL tools/SpectrumDesign.cpp)
target_link_libraries(cd_mechanical_spectrum_design PRIVATE cd_widget_core)
target_compile_options(cd_mechanical_spectrum_design PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_spectrum_design PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_executable(cd_mechanical_input_design EXCLUDE_FROM_ALL tools/InputCounterDesign.cpp)
target_link_libraries(cd_mechanical_input_design PRIVATE cd_widget_core)
target_compile_options(cd_mechanical_input_design PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_input_design PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(TARGET CyberGlass::Widgets)
add_executable(cd_mechanical_lunar_design EXCLUDE_FROM_ALL tools/LunarDesign.cpp)
target_link_libraries(cd_mechanical_lunar_design PRIVATE cd_lunar_core CyberGlass::Widgets)
target_compile_options(cd_mechanical_lunar_design PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_lunar_design PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

endif()

add_executable(cd_mechanical_lunar_animation_tests EXCLUDE_FROM_ALL tools/LunarAnimation.cpp)
target_link_libraries(cd_mechanical_lunar_animation_tests PRIVATE cd_lunar_core)
target_compile_options(cd_mechanical_lunar_animation_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_lunar_animation_tests PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_mechanical_lunar_animation_tests PRIVATE cd_mechanical_calendar_testhost_test_support)
