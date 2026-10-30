include_guard(GLOBAL)
add_library(cd_plugin_runtime_loader STATIC "${CMAKE_CURRENT_LIST_DIR}/RuntimeLibrary.cpp")
target_include_directories(cd_plugin_runtime_loader PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../..")
target_compile_options(cd_plugin_runtime_loader PRIVATE /utf-8 /W4)
target_compile_definitions(cd_plugin_runtime_loader PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
set_target_properties(cd_plugin_runtime_loader PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
