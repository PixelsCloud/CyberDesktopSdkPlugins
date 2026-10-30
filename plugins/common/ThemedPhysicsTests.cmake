include_guard(GLOBAL)
set(themed_physics_root "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(cd_themed_physics_tests EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/ThemedPhysicsTests.cpp")
target_compile_options(cd_themed_physics_tests PRIVATE /utf-8 /W4)
set_target_properties(cd_themed_physics_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
target_link_libraries(cd_themed_physics_tests PRIVATE
    cd_beat_machine_core cd_marble_clock_core cd_physics_toys_core nlohmann_json::nlohmann_json)
target_sources(cd_themed_physics_tests PRIVATE "${themed_physics_root}/plugins/vampire-alice/ToysModel.cpp" "${themed_physics_root}/plugins/vampire-alice/BeatModel.cpp" "${themed_physics_root}/plugins/vampire-alice/MarbleModel.cpp")
target_sources(cd_themed_physics_tests PRIVATE "${themed_physics_root}/plugins/redline-desk/ToysModel.cpp" "${themed_physics_root}/plugins/redline-desk/BeatModel.cpp" "${themed_physics_root}/plugins/redline-desk/MarbleModel.cpp")
target_sources(cd_themed_physics_tests PRIVATE "${themed_physics_root}/plugins/songyun-desk/ToysModel.cpp" "${themed_physics_root}/plugins/songyun-desk/BeatModel.cpp" "${themed_physics_root}/plugins/songyun-desk/MarbleModel.cpp")
target_sources(cd_themed_physics_tests PRIVATE "${themed_physics_root}/plugins/mechanical-desk/ToysModel.cpp")
target_sources(cd_themed_physics_tests PRIVATE "${themed_physics_root}/plugins/mecha-desk/ToysModel.cpp")
