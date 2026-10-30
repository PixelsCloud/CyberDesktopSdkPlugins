# Public native-plugin helper. No Qt or protobuf link dependency.
function(cyberdesktop_add_plugin target)
    cmake_parse_arguments(CD "" "ID;DESCRIPTOR" "SOURCES;ASSETS" ${ARGN})
    if(NOT CD_ID OR NOT CD_DESCRIPTOR OR NOT CD_SOURCES)
        message(FATAL_ERROR "cyberdesktop_add_plugin requires ID, DESCRIPTOR and SOURCES")
    endif()
    add_library(${target} MODULE ${CD_SOURCES})
    target_link_libraries(${target} PRIVATE CyberDesktopSDK::SDK)
    target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD)
    set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${CD_ID}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${CD_ID}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin-build/${CD_ID}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    if(TARGET cd_sdk_cli)
        set(CD_SDK_TOOL "$<TARGET_FILE:cd_sdk_cli>")
        set(CD_SDK_TOOL_DEPENDENCY cd_sdk_cli)
    elseif(TARGET CyberDesktopSDK::Tool)
        set(CD_SDK_TOOL "$<TARGET_FILE:CyberDesktopSDK::Tool>")
        set(CD_SDK_TOOL_DEPENDENCY CyberDesktopSDK::Tool)
    else()
        message(FATAL_ERROR "The installed SDK has no cd-sdk tool; build the SDK with CD_SDK_BUILD_TOOLS=ON")
    endif()
    cd_asset_target(${target}_descriptor ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_DESCRIPTOR}" "$<TARGET_FILE_DIR:${target}>/plugin.textproto"
        COMMAND "${CD_SDK_TOOL}" descriptor "${CD_DESCRIPTOR}" "$<TARGET_FILE_DIR:${target}>/descriptor.pb"
        DEPENDS ${CD_SDK_TOOL_DEPENDENCY} "${CD_DESCRIPTOR}" VERBATIM)
    add_dependencies(${target}_descriptor ${target})
    foreach(asset IN LISTS CD_ASSETS)
        file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" asset_root)
        file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" asset_source)
        cmake_path(IS_PREFIX asset_root "${asset_source}" NORMALIZE asset_is_local)
        if(IS_ABSOLUTE "${asset}" OR asset MATCHES "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR "ASSETS must use paths inside the current source directory")
        endif()
        get_filename_component(asset_directory "${asset}" DIRECTORY)
        if(NOT asset_is_local OR NOT EXISTS "${asset_source}" OR IS_DIRECTORY "${asset_source}")
            message(FATAL_ERROR "ASSETS must reference existing files inside the current source directory: ${asset}")
        endif()
        string(SHA256 asset_target "${asset}")
        cd_asset_target(${target}_asset_${asset_target} ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/${asset_directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "$<TARGET_FILE_DIR:${target}>/${asset}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" VERBATIM)
        add_dependencies(${target}_descriptor ${target}_asset_${asset_target})
    endforeach()
endfunction()
