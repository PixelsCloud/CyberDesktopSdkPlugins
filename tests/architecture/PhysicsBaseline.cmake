include_guard(GLOBAL)
get_filename_component(cd_physics_source_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Optional bounded comparison fixture; no baseline source is part of a release.
set(CD_PHYSICS_BASELINE_ROOT "" CACHE PATH "Prepared pre-refactor physics test inputs")
if(CD_PHYSICS_BASELINE_ROOT)
    add_executable(cd_physics_baseline_tests EXCLUDE_FROM_ALL
        "${cd_physics_source_root}/plugins/common/ThemedPhysicsTests.cpp")
    set(baseline_models plugins/beat-machine/Model.cpp plugins/marble-clock/Model.cpp
        plugins/physics-toys/Model.cpp)
    foreach(theme vampire-alice redline-desk songyun-desk)
        foreach(model Beat Marble Toys)
            list(APPEND baseline_models "plugins/${theme}/${model}Model.cpp")
        endforeach()
    endforeach()
    list(APPEND baseline_models plugins/mechanical-desk/ToysModel.cpp plugins/mecha-desk/ToysModel.cpp)
    foreach(source IN LISTS baseline_models)
        target_sources(cd_physics_baseline_tests PRIVATE "${CD_PHYSICS_BASELINE_ROOT}/${source}")
    endforeach()
    target_include_directories(cd_physics_baseline_tests BEFORE PRIVATE
        "${CD_PHYSICS_BASELINE_ROOT}" "${cd_physics_source_root}")
    target_link_libraries(cd_physics_baseline_tests PRIVATE CyberDesktopSDK::SDK CyberCanvas::PhysicsScene CyberCanvas::Noise
        nlohmann_json::nlohmann_json)
    target_compile_options(cd_physics_baseline_tests PRIVATE /utf-8 /W4)
    set_target_properties(cd_physics_baseline_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
    add_executable(cd_pixel_marble_baseline_tests EXCLUDE_FROM_ALL "${cd_physics_source_root}/plugins/common/PixelMarbleModelTests.cpp"
        "${CD_PHYSICS_BASELINE_ROOT}/pixel-generated/MarbleModel.cpp")
    target_compile_definitions(cd_pixel_marble_baseline_tests PRIVATE CD_PIXEL_MARBLE_BASELINE)
    target_include_directories(cd_pixel_marble_baseline_tests BEFORE PRIVATE
        "${CD_PHYSICS_BASELINE_ROOT}" "${cd_physics_source_root}")
    target_link_libraries(cd_pixel_marble_baseline_tests PRIVATE CyberDesktopSDK::SDK CyberCanvas::PhysicsScene
        nlohmann_json::nlohmann_json)
    add_executable(cd_pixel_marble_model_tests EXCLUDE_FROM_ALL "${cd_physics_source_root}/plugins/common/PixelMarbleModelTests.cpp")
    target_link_libraries(cd_pixel_marble_model_tests PRIVATE cd_marble_clock_core nlohmann_json::nlohmann_json)
    target_include_directories(cd_pixel_marble_model_tests PRIVATE "${cd_physics_source_root}")
    foreach(target cd_pixel_marble_baseline_tests cd_pixel_marble_model_tests)
        target_compile_options(${target} PRIVATE /utf-8 /W4)
        set_target_properties(${target} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
    endforeach()

    add_executable(cd_pixel_physics_baseline_tests EXCLUDE_FROM_ALL "${cd_physics_source_root}/plugins/common/PixelPhysicsModelTests.cpp"
        "${CD_PHYSICS_BASELINE_ROOT}/pixel-generated/BeatModel.cpp"
        "${CD_PHYSICS_BASELINE_ROOT}/pixel-generated/PixelToysModel.cpp"
        "${CD_PHYSICS_BASELINE_ROOT}/pixel-generated/PixelBrickModel.cpp")
    target_compile_definitions(cd_pixel_physics_baseline_tests PRIVATE CD_PIXEL_PHYSICS_BASELINE)
    target_include_directories(cd_pixel_physics_baseline_tests BEFORE PRIVATE
        "${CD_PHYSICS_BASELINE_ROOT}" "${cd_physics_source_root}")
    target_link_libraries(cd_pixel_physics_baseline_tests PRIVATE CyberDesktopSDK::SDK CyberCanvas::PhysicsScene
        CyberCanvas::Noise nlohmann_json::nlohmann_json)
    add_executable(cd_pixel_physics_model_tests EXCLUDE_FROM_ALL "${cd_physics_source_root}/plugins/common/PixelPhysicsModelTests.cpp")
    target_link_libraries(cd_pixel_physics_model_tests PRIVATE cd_beat_machine_core cd_physics_toys_core cd_brick_clock_core
        nlohmann_json::nlohmann_json)
    target_include_directories(cd_pixel_physics_model_tests PRIVATE "${cd_physics_source_root}")
    foreach(target cd_pixel_physics_baseline_tests cd_pixel_physics_model_tests)
        target_compile_options(${target} PRIVATE /utf-8 /W4)
        set_target_properties(${target} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
    endforeach()

endif()
