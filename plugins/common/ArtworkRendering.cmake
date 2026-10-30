include_guard(GLOBAL)
find_package(nlohmann_json CONFIG REQUIRED)
# Shared artwork registration, native-DPI caches, typography and controls.
# Keep the original C++ namespace and DLL exports compatible with installed skins.
add_library(cd_artwork_rendering STATIC "${CMAKE_CURRENT_LIST_DIR}/../mecha-desk/Skin.cpp")
target_link_libraries(cd_artwork_rendering PUBLIC CyberDesktopSDK::SDK PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(cd_artwork_rendering PRIVATE /utf-8 /W4)
set_target_properties(cd_artwork_rendering PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(cd_artwork_rendering PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../..")
