configure_file(cmake/Version.h.in include/sdk/Version.h @ONLY)
set(CD_SDK_HEADERS PluginApi.h PackageApi.h Settings.h WeatherPresentation.h WindowsFonts.h WindowsRendering.h Canvas.h RenderOutput.h Interaction.h Environment.h Diagnostics.h Scene.h Keyboard.h SceneList.h List.h Images.h Update.h Icons.h Typography.h Controls.h Language.h UiTranslations.h)
foreach(header IN LISTS CD_SDK_HEADERS)
    configure_file("${header}" "include/sdk/${header}" COPYONLY)
endforeach()
add_library(cd_sdk_abi INTERFACE)
add_library(CyberDesktopSDK::ABI ALIAS cd_sdk_abi)
set_target_properties(cd_sdk_abi PROPERTIES EXPORT_NAME ABI)
target_include_directories(cd_sdk_abi INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(cd_sdk_abi INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_library(cd_sdk_core STATIC support/Settings.cpp support/Environment.cpp support/DiagnosticHost.cpp
    support/Services.cpp support/Snapshots.cpp support/Plugin.cpp support/WeatherPresentation.cpp)
add_library(CyberDesktopSDK::Core ALIAS cd_sdk_core)
set_target_properties(cd_sdk_core PROPERTIES EXPORT_NAME Core)
target_link_libraries(cd_sdk_core PUBLIC cd_sdk_abi)
target_compile_features(cd_sdk_core PUBLIC cxx_std_20)
target_compile_options(cd_sdk_core PRIVATE /utf-8)

add_library(cd_sdk_rendering STATIC support/Canvas.cpp support/RenderOutput.cpp support/Language.cpp
    support/WindowsFonts.cpp support/WindowsRendering.cpp support/Update.cpp support/Interaction.cpp
    support/Icons.cpp support/Diagnostics.cpp)
add_library(CyberDesktopSDK::Rendering ALIAS cd_sdk_rendering)
set_target_properties(cd_sdk_rendering PROPERTIES EXPORT_NAME Rendering)
target_link_libraries(cd_sdk_rendering PUBLIC cd_sdk_core CyberCanvas::CyberCanvas)
target_compile_options(cd_sdk_rendering PRIVATE /utf-8)

add_library(cd_sdk_images STATIC support/Images.cpp)
add_library(CyberDesktopSDK::Images ALIAS cd_sdk_images)
set_target_properties(cd_sdk_images PROPERTIES EXPORT_NAME Images)
target_link_libraries(cd_sdk_images PUBLIC cd_sdk_abi CyberCanvas::Resources)
target_compile_options(cd_sdk_images PRIVATE /utf-8)
add_library(cd_sdk_layout STATIC support/Controls.cpp support/Typography.cpp support/Keyboard.cpp support/List.cpp
    support/Scene.cpp support/SceneList.cpp)
add_library(CyberDesktopSDK::Layout ALIAS cd_sdk_layout)
set_target_properties(cd_sdk_layout PROPERTIES EXPORT_NAME Layout)
target_link_libraries(cd_sdk_layout PUBLIC cd_sdk_rendering cd_sdk_images CyberCanvas::Layout)
target_compile_options(cd_sdk_layout PRIVATE /utf-8)

# Source-compatible umbrella. New consumers select only the capabilities they use.
add_library(cd_public_sdk INTERFACE)
add_library(CyberDesktopSDK::SDK ALIAS cd_public_sdk)
set_target_properties(cd_public_sdk PROPERTIES EXPORT_NAME SDK)
target_compile_features(cd_public_sdk INTERFACE cxx_std_20)
target_link_libraries(cd_public_sdk INTERFACE cd_sdk_rendering cd_sdk_images cd_sdk_layout)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/protobuf")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/protobuf/plugin_package.pb.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/protobuf/plugin_package.pb.h"
    COMMAND "${CD_SDK_PROTOC}" --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/protobuf
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/schemas
        ${CMAKE_CURRENT_SOURCE_DIR}/schemas/plugin_package.proto
    DEPENDS schemas/plugin_package.proto "${CD_SDK_PROTOC}" VERBATIM)
add_library(cd_sdk_support STATIC support/Package.cpp support/BinaryImports.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/protobuf/plugin_package.pb.cc")
target_include_directories(cd_sdk_support PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/support>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/protobuf>)
target_link_libraries(cd_sdk_support PUBLIC cd_sdk_core protobuf::libprotobuf PRIVATE bcrypt)
target_compile_options(cd_sdk_support PRIVATE /utf-8)
set_target_properties(cd_sdk_support PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(cd_sdk_runtime SHARED support/PackageRuntime.cpp)
add_library(CyberDesktopSDK::PackageRuntime ALIAS cd_sdk_runtime)
set_target_properties(cd_sdk_runtime PROPERTIES EXPORT_NAME PackageRuntime OUTPUT_NAME cyberdesktop_sdk_runtime)
target_compile_definitions(cd_sdk_runtime PRIVATE CD_SDK_RUNTIME_BUILD)
target_compile_options(cd_sdk_runtime PRIVATE /utf-8)
target_link_libraries(cd_sdk_runtime PRIVATE cd_sdk_support)
target_link_libraries(cd_sdk_runtime PUBLIC cd_sdk_abi)
