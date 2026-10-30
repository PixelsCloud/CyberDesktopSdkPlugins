cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CyberDesktopWidgets VERSION 1.0.0 LANGUAGES CXX)
    find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
if(MSVC AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Widgets require Windows x64 MSVC /MT")
endif()
set(CMAKE_CXX_STANDARD 20)
option(CD_WIDGET_BUILD_TESTS "Build focused widget checks and design exporter" ${PROJECT_IS_TOP_LEVEL})
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_widget_sdk_tool CD_WIDGET_LICENSE)
cd_plugin_canvas_license(tweeny CD_WIDGET_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_WIDGET_YOGA_LICENSE)

add_library(cd_widget_core STATIC Instance.cpp Canvas.cpp Matrix.cpp MatrixViews.inl Player.cpp PlayerViews.inl InputStatsViews.inl Spectrum.cpp Calendar.cpp Album.cpp)
target_link_libraries(cd_widget_core PUBLIC CyberDesktopSDK::SDK PRIVATE CyberCanvas::Effects d2d1 dwrite windowscodecs ole32 shell32 propsys)
target_include_directories(cd_widget_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_definitions(cd_widget_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(cd_widget_core PRIVATE /utf-8 /W4)
set_target_properties(cd_widget_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count}-1")
set(widget_catalog "#pragma once\n#include <array>\n#include \"Widget.h\"\nnamespace widgetplugin {\nstruct CatalogItem { Spec spec; const wchar_t* folder; };\ninline constexpr std::array<CatalogItem, ${count}> catalog{{\n")
foreach(index RANGE ${last})
    foreach(key id folder kind rows columns)
        string(JSON ${key} GET "${catalog}" ${index} ${key})
    endforeach()
    string(APPEND widget_catalog "{{\"${id}\", Kind(${kind}), ${rows}, ${columns}}, L\"${folder}\"},\n")
    string(REPLACE "-" "_" target "cd_widget_${folder}")
    set(package_input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_widget_core)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_WIDGET_ID="${id}" CD_WIDGET_KIND=${kind} CD_WIDGET_ROWS=${rows} CD_WIDGET_COLUMNS=${columns})
    target_compile_options(${target} PRIVATE /utf-8)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}" PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/${folder}")
    cd_asset_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses" "${CMAKE_BINARY_DIR}/widget-packages" "${package_input}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "$<TARGET_FILE_DIR:${target}>/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_WIDGET_LICENSE}" "$<TARGET_FILE_DIR:${target}>/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "$<TARGET_FILE_DIR:${target}>/preview.png"
        COMMAND $<TARGET_FILE:cd_widget_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "$<TARGET_FILE_DIR:${target}>/descriptor.pb"
        # Use declared deployment files; old DLL/PDB/manifest files can remain in an
        # incremental compiler output directory and must never enter a package.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${package_input}/plugin.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:${target}>/descriptor.pb" "${package_input}/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${package_input}/preview.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${package_input}/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_WIDGET_LICENSE}" "${package_input}/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_WIDGET_TWEENY_LICENSE}" "${package_input}/licenses/Tweeny-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_WIDGET_YOGA_LICENSE}" "${package_input}/licenses/Yoga-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${package_input}/licenses" "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND $<TARGET_FILE:cd_widget_sdk_tool> pack "${package_input}" "${CMAKE_BINARY_DIR}/widget-packages/${id}.cdpkg"
        DEPENDS ${target} cd_widget_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" VERBATIM)
endforeach()
string(APPEND widget_catalog "}};\n}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/WidgetCatalog.h" CONTENT "${widget_catalog}")
if(CD_WIDGET_BUILD_TESTS)
    enable_testing()
    add_executable(cd_widget_tests tools/Tests.cpp)
    add_executable(cd_widget_design tools/Design.cpp)
    foreach(target cd_widget_tests cd_widget_design)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(${target} PRIVATE cd_widget_core)
        target_compile_options(${target} PRIVATE /utf-8 /W4)
        set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    endforeach()
    add_test(NAME widgets COMMAND cd_widget_tests)
    include("${CMAKE_CURRENT_LIST_DIR}/tools/TestAssets.cmake")
    if(TARGET CyberGlass::Widgets AND NOT TARGET cd_player_design)
        add_executable(cd_player_design tools/PlayerDesign.cpp)
        target_link_libraries(cd_player_design PRIVATE cd_widget_core)
        target_compile_options(cd_player_design PRIVATE /utf-8 /W4)
        set_target_properties(cd_player_design PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    endif()
endif()

# Implementations shared by this plugin family.
target_sources(cd_widget_core PRIVATE AlbumArtwork.cpp InputCounter.cpp MechanicalInputCounter.cpp MechanicalSpectrum.cpp PlayerArtwork.cpp
    ThemedController.h ThemedController.cpp MediaController.h MediaController.cpp)
add_executable(cd_themed_widget_tests EXCLUDE_FROM_ALL tools/ThemedControllerTests.cpp)
target_link_libraries(cd_themed_widget_tests PRIVATE cd_widget_core)
target_compile_options(cd_themed_widget_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_themed_widget_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
enable_testing()
add_test(NAME themed_widget_controller COMMAND cd_themed_widget_tests)
set_tests_properties(themed_widget_controller PROPERTIES TIMEOUT 30)

add_executable(cd_media_controller_tests EXCLUDE_FROM_ALL tools/MediaControllerTests.cpp)
target_link_libraries(cd_media_controller_tests PRIVATE cd_widget_core)
target_compile_options(cd_media_controller_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_media_controller_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
add_test(NAME themed_media_controller COMMAND cd_media_controller_tests)
set_tests_properties(themed_media_controller PROPERTIES TIMEOUT 30)

include("${CMAKE_CURRENT_LIST_DIR}/../common/Artwork.cmake")
target_link_libraries(cd_widget_core PUBLIC cd_mechanical_art)
