include_guard(GLOBAL)

# Pixel renderers compile a separate controller variant. Keep the volume state
# transition in one library instead of recompiling it for every renderer.
add_library(cd_productivity_volume STATIC "${CMAKE_CURRENT_LIST_DIR}/../productivity-suite/Volume.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../productivity-suite/Feed.cpp")
target_link_libraries(cd_productivity_volume PRIVATE CyberDesktopSDK::Core)
target_include_directories(cd_productivity_volume PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
target_compile_options(cd_productivity_volume PRIVATE /utf-8 /W4)
set_target_properties(cd_productivity_volume PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
