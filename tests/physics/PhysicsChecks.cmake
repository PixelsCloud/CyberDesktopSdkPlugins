include_guard(GLOBAL)
if(TARGET cd_sdk_tool_support)
    add_executable(cd_background_models_tests EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/BackgroundModelsTest.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../plugins/redline-desk/Kinetics.cpp")
    target_link_libraries(cd_background_models_tests PRIVATE cd_beat_machine_core cd_hourglass_core cd_streetlink_physics_model
        cd_marble_clock_core cd_brick_clock_core cd_deep_whale_beat_scene
        CyberDesktopSDK::BackgroundReference)
    target_include_directories(cd_background_models_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
    target_compile_options(cd_background_models_tests PRIVATE /utf-8 /W4)
    set_target_properties(cd_background_models_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
    add_executable(cd_physics_capacity_tests EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/ModelCapacityTests.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../plugins/streetlink-desk/Physics.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../plugins/redline-desk/Kinetics.cpp")
    target_link_libraries(cd_physics_capacity_tests PRIVATE cd_sdk_tool_support cd_beat_machine_core cd_marble_clock_core
        cd_physics_toys_core cd_hourglass_core cd_brick_clock_core cd_deep_whale_beat_scene cd_artwork_rendering
        cd_streetlink_physics_model nlohmann_json::nlohmann_json)
    target_include_directories(cd_physics_capacity_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
    target_compile_options(cd_physics_capacity_tests PRIVATE /utf-8 /W4)
    set_target_properties(cd_physics_capacity_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
    add_executable(cd_physics_plugins_tests EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/PluginPhysicsTests.cpp")
    target_link_libraries(cd_physics_plugins_tests PRIVATE cd_sdk_tool_support nlohmann_json::nlohmann_json)
    get_filename_component(cd_physics_public_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    target_include_directories(cd_physics_plugins_tests PRIVATE "${cd_physics_public_root}")
    target_compile_options(cd_physics_plugins_tests PRIVATE /utf-8 /W4)
    set_target_properties(cd_physics_plugins_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
endif()
