cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_sign_sdk_tool CD_SIGN_LICENSE)
cd_plugin_canvas_license(tweeny CD_SIGN_TWEENY_LICENSE)
cd_plugin_canvas_license(yoga CD_SIGN_YOGA_LICENSE)
add_library(cd_neon_sign_core STATIC Sign.cpp Animation.cpp)
target_include_directories(cd_neon_sign_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_neon_sign_core PUBLIC CyberDesktopSDK::SDK)
target_compile_options(cd_neon_sign_core PRIVATE /utf-8 /W4)
set_target_properties(cd_neon_sign_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_library(cd_neon_sign MODULE Plugin.cpp)
add_library(cd_neon_sign_minimal MODULE Plugin.cpp)
add_executable(cd_neon_sign_tests EXCLUDE_FROM_ALL Tests.cpp)
foreach(target cd_neon_sign cd_neon_sign_minimal cd_neon_sign_tests)
    target_link_libraries(${target} PRIVATE cd_neon_sign_core)
    target_link_libraries(${target} PRIVATE CyberDesktopSDK::SDK d2d1 dwrite windowscodecs ole32)
    target_compile_options(${target} PRIVATE /utf-8 /W4)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
foreach(variant full minimal)
    if(variant STREQUAL "full")
        set(target cd_neon_sign)
        set(id com.cyberdesktop.neon.sign)
        set(version 1.2.0)
        set(resources "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        set(target cd_neon_sign_minimal)
        set(id com.cyberdesktop.neon.sign.1x4)
        set(version 1.1.0)
        set(resources "${CMAKE_CURRENT_SOURCE_DIR}/minimal")
    endif()
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_SIGN_ID="${id}" CD_SIGN_VERSION="${version}")
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${variant}")
    set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
    cd_asset_target(${target}_package ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/licenses" "${CMAKE_BINARY_DIR}/sign-packages"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
        COMMAND $<TARGET_FILE:cd_sign_sdk_tool> descriptor "${resources}/plugin.textproto" "${input}/descriptor.pb"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${resources}/preview.png" "${input}/preview.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_SIGN_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_SIGN_TWEENY_LICENSE}" "${input}/licenses/Tweeny-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_SIGN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
        COMMAND $<TARGET_FILE:cd_sign_sdk_tool> pack "${input}" "${CMAKE_BINARY_DIR}/sign-packages/${id}.cdpkg"
        DEPENDS ${target} cd_sign_sdk_tool "${resources}/plugin.textproto" VERBATIM)
endforeach()
