cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")
find_package(nlohmann_json CONFIG REQUIRED)

add_library(cd_mechanical_desk_runtime SHARED Runtime.cpp Skin.cpp BasicClock.cpp BasicClockInstance.cpp
  ProductivityRenderer.cpp SpectrumRenderer.cpp PondRenderer.cpp PondInstance.cpp PondHabitat.cpp BrickRenderer.cpp BrickInstance.cpp
  ChimeRenderer.cpp ToysModel.cpp AmbienceRenderer.cpp controllers/productivity/Instance.cpp
  controllers/widget/Instance.cpp controllers/ambience/Instance.cpp controllers/toys/Instance.cpp)
target_compile_definitions(cd_mechanical_desk_runtime PRIVATE CD_MECHANICAL_DESK_RUNTIME_BUILD)
target_include_directories(cd_mechanical_desk_runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../widget-suite")
target_link_libraries(cd_mechanical_desk_runtime PRIVATE cd_clock_core cd_productivity_core cd_widget_core cd_koi_core
  cd_ambience_core cd_physics_toys_core cd_brick_clock_core nlohmann_json::nlohmann_json)
target_compile_options(cd_mechanical_desk_runtime PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_desk_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" OUTPUT_NAME cyberdesktop_mechanical_desk_runtime
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
set(stages)
set(packages)
foreach(index RANGE 0 ${last})
  foreach(key id folder family kind rows columns)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_mechanical_desk_${folder}")
  add_library(${target} MODULE Plugin.cpp)
  target_include_directories(${target} PRIVATE "${CD_PLUGIN_SOURCE_ROOT}")
  target_link_libraries(${target} PRIVATE cd_mechanical_desk_runtime CyberDesktopSDK::ABI)
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_MECHANICAL_DESK_ID="${id}"
    CD_MECHANICAL_DESK_FAMILY="${family}" CD_MECHANICAL_DESK_KIND="${kind}"
    CD_MECHANICAL_DESK_ROWS=${rows} CD_MECHANICAL_DESK_COLUMNS=${columns})
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  cd_asset_target(${target}_stage
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cd_mechanical_desk_runtime>" "${input}/cyberdesktop_mechanical_desk_runtime.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/${folder}" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/koi-suite/licenses" "${input}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CD_PLUGIN_SOURCE_ROOT}/plugins/productivity-suite/licenses" "${input}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    DEPENDS ${target} cd_plugin_package_tool VERBATIM)
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/mechanical-desk-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${input}/preview.png" "$<TARGET_FILE_DIR:${target}>/preview.png"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mechanical-desk-packages/${id}.cdpkg"
    DEPENDS ${target}_stage VERBATIM)
  list(APPEND stages ${target}_stage)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_mechanical_desk_stage DEPENDS ${stages})
add_custom_target(cd_mechanical_desk_packages DEPENDS ${packages})
