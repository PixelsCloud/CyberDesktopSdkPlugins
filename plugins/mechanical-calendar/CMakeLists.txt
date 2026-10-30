cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_mechanical_calendar_sdk_tool CD_MECHANICAL_CALENDAR_LICENSE)
cd_plugin_canvas_license(tweeny CD_MECHANICAL_CALENDAR_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_MECHANICAL_CALENDAR_YOGA_LICENSE)

# Reuse the existing Gregorian month calculations; leave original plugins intact.
add_library(cd_mechanical_calendar_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_link_libraries(cd_mechanical_calendar_core PUBLIC cd_widget_core)
target_include_directories(cd_mechanical_calendar_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_mechanical_calendar_core PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_calendar_core PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

set(id "com.cyberdesktop.calendar.mechanical.4x2")
set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
add_library(cd_mechanical_calendar_4x2 MODULE Plugin.cpp)
target_link_libraries(cd_mechanical_calendar_4x2 PRIVATE cd_mechanical_calendar_core)
target_compile_definitions(cd_mechanical_calendar_4x2 PRIVATE CD_PLUGIN_BUILD)
target_compile_options(cd_mechanical_calendar_4x2 PRIVATE /utf-8 /W4)
target_link_options(cd_mechanical_calendar_4x2 PRIVATE /INCREMENTAL:NO)
set_target_properties(cd_mechanical_calendar_4x2 PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects")
cd_asset_target(cd_mechanical_calendar_packages ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses"
    "${CMAKE_BINARY_DIR}/mechanical-calendar-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:cd_mechanical_calendar_4x2>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_mechanical_calendar_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/4x2/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/4x2/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/body.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/provenance.json" "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MECHANICAL_CALENDAR_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MECHANICAL_CALENDAR_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_MECHANICAL_CALENDAR_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Lucide-ISC.txt" "${input}/licenses/Lucide-ISC.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:cd_mechanical_calendar_4x2>"
    COMMAND $<TARGET_FILE:cd_mechanical_calendar_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/mechanical-calendar-packages/${id}.cdpkg"
    DEPENDS cd_mechanical_calendar_4x2 cd_mechanical_calendar_sdk_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/4x2/plugin.textproto"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/body.png" VERBATIM)

add_executable(cd_mechanical_calendar_tests EXCLUDE_FROM_ALL tools/Tests.cpp)
set(calendar_tools cd_mechanical_calendar_tests)
if(TARGET CyberGlass::Widgets)
    add_executable(cd_mechanical_calendar_design EXCLUDE_FROM_ALL tools/Design.cpp)
    target_link_libraries(cd_mechanical_calendar_design PRIVATE CyberGlass::Widgets)
    list(APPEND calendar_tools cd_mechanical_calendar_design)
endif()
foreach(target IN LISTS calendar_tools)
    target_link_libraries(${target} PRIVATE cd_mechanical_calendar_core)
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    set_target_properties(${target} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

add_library(cd_mechanical_calendar_testhost_test_support STATIC EXCLUDE_FROM_ALL tools/TestHost.cpp)
target_link_libraries(cd_mechanical_calendar_testhost_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_mechanical_calendar_testhost_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_mechanical_calendar_testhost_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_calendar_testhost_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_mechanical_calendar_tests PRIVATE cd_mechanical_calendar_testhost_test_support)

if(TARGET cd_mechanical_calendar_design)
    target_link_libraries(cd_mechanical_calendar_design PRIVATE cd_mechanical_calendar_testhost_test_support)
endif()
