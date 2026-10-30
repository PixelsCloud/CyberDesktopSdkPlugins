cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_precision_sdk_tool CD_PRECISION_LICENSE)
add_library(cd_precision_clocks_core STATIC Model.cpp Renderer.cpp Instance.cpp)
target_include_directories(cd_precision_clocks_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_precision_clocks_core PUBLIC CyberDesktopSDK::SDK cd_lunar_calendar)
target_compile_definitions(cd_precision_clocks_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(cd_precision_clocks_core PRIVATE /utf-8 /W4)
set_target_properties(cd_precision_clocks_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set(index 0)
set(packages)
foreach(kind skeleton green dress)
    set(id "com.cyberdesktop.clock.precision.${kind}.2x2")
    set(target "cd_precision_clock_${kind}")
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_precision_clocks_core)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_PRECISION_KIND=${index})
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${kind}/link"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
    cd_asset_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/precision-clocks-packages"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
        COMMAND $<TARGET_FILE:cd_precision_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${kind}/plugin.textproto" "${input}/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${kind}/preview.png" "${input}/preview.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PRECISION_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sxtwl/LICENSE" "${input}/licenses/sxtwl-BSD-3-Clause.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/${kind}" "${input}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND $<TARGET_FILE:cd_precision_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/precision-clocks-packages/${id}.cdpkg"
        DEPENDS ${target} cd_precision_sdk_tool VERBATIM)
    list(APPEND packages ${target}_package)
    math(EXPR index "${index}+1")
endforeach()
add_custom_target(cd_precision_clocks_packages DEPENDS ${packages})
foreach(tool Tests Design)
    string(TOLOWER "${tool}" lower)
    add_executable(cd_precision_clocks_${lower} EXCLUDE_FROM_ALL "tools/${tool}.cpp")
    target_link_libraries(cd_precision_clocks_${lower} PRIVATE cd_precision_clocks_core)
    target_compile_definitions(cd_precision_clocks_${lower} PRIVATE CD_PRECISION_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
        CD_PRECISION_PLUGINS="${CMAKE_BINARY_DIR}/plugins")
    target_compile_options(cd_precision_clocks_${lower} PRIVATE /utf-8 /W4)
    set_target_properties(cd_precision_clocks_${lower} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()

add_library(cd_precision_clocks_testhost_test_support STATIC EXCLUDE_FROM_ALL tools/TestHost.cpp)
target_link_libraries(cd_precision_clocks_testhost_test_support PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_precision_clocks_testhost_test_support PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_precision_clocks_testhost_test_support PRIVATE /utf-8 /W4)
set_target_properties(cd_precision_clocks_testhost_test_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_link_libraries(cd_precision_clocks_tests PRIVATE cd_precision_clocks_testhost_test_support)

target_compile_definitions(cd_precision_clocks_testhost_test_support PRIVATE CD_PRECISION_PLUGINS="${CMAKE_BINARY_DIR}/plugins")
