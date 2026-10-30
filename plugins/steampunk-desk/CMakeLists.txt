cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")

# Only implemented families enter the build. The catalog also records the
# remaining approved designs; active.json explicitly selects runtime packages.
add_library(cd_steam_desk_core STATIC Model.cpp Renderer.cpp ClockInstance.cpp)
target_link_libraries(cd_steam_desk_core PUBLIC cd_mechanical_pixels_core)
add_library(cd_steam_desk_spring_core STATIC SpringClock.cpp)
target_link_libraries(cd_steam_desk_spring_core PUBLIC cd_physics_toys_core cd_mechanical_pixels_core)
add_library(cd_steam_desk_marble_core STATIC MarbleClock.cpp)
target_link_libraries(cd_steam_desk_marble_core PUBLIC cd_marble_clock_core cd_mechanical_pixels_core)
foreach(target cd_steam_desk_core cd_steam_desk_spring_core cd_steam_desk_marble_core)
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  set_target_properties(${target} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/active.json" active)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS catalog.json active.json)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
foreach(index RANGE 0 ${last})
  foreach(key id folder art family)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(FIND "${active}" "${id}" enabled)
  if(enabled EQUAL -1)
    continue()
  endif()
  string(REPLACE "-" "_" target "cd_steam_desk_${folder}")
  if(family STREQUAL "clock")
    set(entry Plugin.cpp)
    set(core cd_steam_desk_core)
  elseif(family STREQUAL "spring")
    set(entry SpringPlugin.cpp)
    set(core cd_steam_desk_spring_core)
  elseif(family STREQUAL "marble")
    set(entry MarblePlugin.cpp)
    set(core cd_steam_desk_marble_core)
  else()
    message(FATAL_ERROR "Steamworks renderer not implemented: ${family}")
  endif()
  add_library(${target} MODULE ${entry})
  target_link_libraries(${target} PRIVATE ${core})
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_STEAM_DESK_ID="${id}")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")

  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(materials body.png provenance.json)
  set(extra)
  if(family STREQUAL "marble")
    list(APPEND materials precision-gear.png clock-window.png)
  endif()
  set(art_dependencies)
  foreach(material IN LISTS materials)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/${material}")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/${material}" "${input}/assets")
  endforeach()
  if(family STREQUAL "spring" OR family STREQUAL "marble")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
  endif()
  if(family STREQUAL "spring")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt")
  endif()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses"
      "${CMAKE_BINARY_DIR}/steampunk-desk-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor
      "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}"
      "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    ${extra}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/steampunk-desk-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto"
      "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" ${art_dependencies} VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_steam_desk_packages DEPENDS ${packages})

foreach(family clock spring marble)
  if(family STREQUAL "clock")
    set(verifier cd_steam_desk_verify)
    set(source tools/Verify.cpp)
    set(core cd_steam_desk_core)
  elseif(family STREQUAL "spring")
    set(verifier cd_steam_desk_spring_verify)
    set(source tools/SpringVerify.cpp)
    set(core cd_steam_desk_spring_core)
  else()
    set(verifier cd_steam_desk_marble_verify)
    set(source tools/MarbleVerify.cpp)
    set(core cd_steam_desk_marble_core)
  endif()
  add_executable(${verifier} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${verifier} PRIVATE ${core})
  target_compile_options(${verifier} PRIVATE /utf-8 /W4)
  set_target_properties(${verifier} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

# Implementations shared by this plugin family.


target_link_libraries(cd_steam_desk_verify PRIVATE cd_mechanical_pixels_testhost_test_support)

target_link_libraries(cd_steam_desk_spring_verify PRIVATE cd_mechanical_pixels_testhost_test_support)

target_link_libraries(cd_steam_desk_marble_verify PRIVATE cd_mechanical_pixels_testhost_test_support)

add_library(cd_steampunk_desk_support_test_support STATIC EXCLUDE_FROM_ALL tools/Support.cpp)
target_link_libraries(cd_steampunk_desk_support_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_steampunk_desk_support_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CD_PLUGIN_SOURCE_ROOT}")
target_compile_options(cd_steampunk_desk_support_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_steampunk_desk_support_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_steam_desk_verify PRIVATE cd_steampunk_desk_support_test_support)

target_link_libraries(cd_steam_desk_spring_verify PRIVATE cd_steampunk_desk_support_test_support)

target_link_libraries(cd_steam_desk_marble_verify PRIVATE cd_steampunk_desk_support_test_support)

# Shared case artwork is independent of any individual widget model.
add_library(cd_steam_desk_artwork STATIC SteamDigits.cpp SteamParts.cpp SteamSprite.cpp)
target_include_directories(cd_steam_desk_artwork PUBLIC "${CD_PLUGIN_SOURCE_ROOT}")
target_link_libraries(cd_steam_desk_artwork PUBLIC CyberDesktopSDK::SDK)
target_compile_options(cd_steam_desk_artwork PRIVATE /utf-8 /W4)
set_target_properties(cd_steam_desk_artwork PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
foreach(target cd_steam_desk_core cd_steam_desk_spring_core cd_steam_desk_marble_core)
  target_link_libraries(${target} PUBLIC cd_steam_desk_artwork)
endforeach()
