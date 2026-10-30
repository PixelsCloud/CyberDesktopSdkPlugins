find_package(nlohmann_json 3.12 CONFIG REQUIRED)
add_library(cd_sdk_tool_support STATIC EXCLUDE_FROM_ALL
    tools/Runtime.cpp tools/SettingsProbe.cpp tools/PreviewOptions.cpp tools/ResourceWatcher.cpp
    tools/PreviewImages.cpp tools/Diagnostics.cpp tools/PreviewState.cpp tools/PreviewReplay.cpp tools/PreviewPresets.cpp
    tools/PreviewServices.cpp tools/PreviewTextInput.cpp tools/WindowsApartment.cpp tools/PreviewOutput.cpp)
target_link_libraries(cd_sdk_tool_support PUBLIC cd_sdk_support CyberCanvas::Native
    PRIVATE CyberCanvas::CyberCanvas CyberCanvas::Resources nlohmann_json::nlohmann_json user32 gdi32 shell32 uuid comctl32)
target_compile_options(cd_sdk_tool_support PRIVATE /utf-8)
target_include_directories(cd_sdk_tool_support PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreviewIcons.cmake")
if(CD_SDK_BUILD_TOOLS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundledFonts.cmake")
    add_executable(cd_sdk_cli tools/SdkCli.cpp)
    set_target_properties(cd_sdk_cli PROPERTIES OUTPUT_NAME cd-sdk)
    target_compile_options(cd_sdk_cli PRIVATE /utf-8)
    # The CLI also performs WIC image comparisons; only that private platform
    # helper needs Canvas, not the package runtime or the public ABI target.
    target_link_libraries(cd_sdk_cli PRIVATE cd_sdk_tool_support)
    add_executable(cd_sdk_preview tools/Preview.cpp)
    set_target_properties(cd_sdk_preview PROPERTIES OUTPUT_NAME cd-sdk-preview)
    target_compile_options(cd_sdk_preview PRIVATE /utf-8)
    target_link_libraries(cd_sdk_preview PRIVATE cd_sdk_tool_support CyberDesktopSDK::Rendering CyberDesktopSDK::Images
        user32 gdi32 ole32 shell32 uuid)
    install(TARGETS cd_sdk_cli cd_sdk_preview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
add_library(cd_sdk_preview_ui STATIC EXCLUDE_FROM_ALL tools/NativeSettings.cpp tools/PreviewOverlay.cpp)
target_link_libraries(cd_sdk_preview_ui PUBLIC cd_sdk_tool_support cd_sdk_rendering)
target_compile_options(cd_sdk_preview_ui PRIVATE /utf-8)
if(TARGET cd_sdk_preview)
    target_link_libraries(cd_sdk_preview PRIVATE cd_sdk_preview_ui)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreviewLive2D.cmake")
