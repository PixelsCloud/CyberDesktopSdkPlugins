add_executable(cd_render_cache_tests EXCLUDE_FROM_ALL RenderCacheTest.cpp)
target_link_libraries(cd_render_cache_tests PRIVATE cd_brick_clock_core cd_marble_clock_core cd_precision_clocks_core)
target_compile_options(cd_render_cache_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_render_cache_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
enable_testing()
add_test(NAME plugin_render_cache COMMAND cd_render_cache_tests)
set_tests_properties(plugin_render_cache PROPERTIES TIMEOUT 60)
