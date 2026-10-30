include_guard(GLOBAL)
get_filename_component(cd_architecture_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(cd_public_structure_check
    COMMAND "${Python3_EXECUTABLE}" "${cd_architecture_root}/tests/architecture/check_structure.py"
        --root "${cd_architecture_root}"
    COMMAND "${Python3_EXECUTABLE}" "${cd_architecture_root}/tests/architecture/test_structure.py"
    COMMAND "${Python3_EXECUTABLE}" "${cd_architecture_root}/tests/architecture/test_dependencies.py"
    VERBATIM)

if(TARGET cd_widget_core AND TARGET cd_productivity_core AND TARGET cd_monitor_core AND
   TARGET cd_ambience_core AND TARGET cd_beat_machine_core AND TARGET cd_marble_clock_core AND
   TARGET cd_physics_toys_core)
    set(cd_shared_headers
        plugins/widget-suite/ThemedController.h plugins/widget-suite/MediaController.h
        plugins/productivity-suite/ThemedController.h plugins/system-info-suite/ThemedController.h
        plugins/ambience-suite/ThemedController.h plugins/beat-machine/Model.h
        plugins/marble-clock/Model.h plugins/physics-toys/Model.h
        plugins/pixel-desk/PixelAmbience.h plugins/pixel-desk/PixelPlayer.h
        plugins/pixel-desk/PixelMonitor.h plugins/pixel-desk/PixelProductivity.h
        plugins/pixel-desk/PixelToysModel.h plugins/pixel-desk/MarbleClock.h
        plugins/pixel-desk/PixelBeat.h plugins/pixel-desk/PixelChime.h)
    set(cd_shared_header_sources)
    foreach(header IN LISTS cd_shared_headers)
        string(MAKE_C_IDENTIFIER "${header}" name)
        set(source "${CMAKE_BINARY_DIR}/Testing/header-selfcontain/public_${name}.cpp")
        file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
        list(APPEND cd_shared_header_sources "${source}")
    endforeach()
    add_library(cd_shared_header_checks OBJECT EXCLUDE_FROM_ALL ${cd_shared_header_sources})
    target_link_libraries(cd_shared_header_checks PRIVATE cd_widget_core cd_productivity_core cd_monitor_core
        cd_ambience_core cd_beat_machine_core cd_marble_clock_core cd_physics_toys_core)
    target_include_directories(cd_shared_header_checks PRIVATE "${cd_architecture_root}")
    target_compile_options(cd_shared_header_checks PRIVATE /utf-8 /W4)
    set_target_properties(cd_shared_header_checks PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)

    # A bounded fault injection in one generated test translation unit. Production
    # sources are never edited; the deliberately broken binary is not packaged.
    set(controller "${cd_architecture_root}/plugins/ambience-suite/ThemedController.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${controller}")
    file(READ "${controller}" body)
    set(cleanup "            ambient.close(context, audio);")
    string(FIND "${body}" "${cleanup}" position)
    if(position LESS 0)
        message(FATAL_ERROR "Update the lifecycle negative control for the current destructor")
    endif()
    string(REPLACE "${cleanup}" "            /* fault injection: omitted audio cleanup */" mutant "${body}")
    set(source "${CMAKE_BINARY_DIR}/Testing/architecture-negative/AmbienceController.cpp")
    file(GENERATE OUTPUT "${source}" CONTENT "${mutant}")
    add_executable(cd_ambience_lifecycle_negative EXCLUDE_FROM_ALL "${source}"
        "${cd_architecture_root}/plugins/ambience-suite/tools/ThemedControllerTests.cpp")
    target_link_libraries(cd_ambience_lifecycle_negative PRIVATE cd_ambience_core)
    target_compile_options(cd_ambience_lifecycle_negative PRIVATE /utf-8 /W4)
    set_target_properties(cd_ambience_lifecycle_negative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        DISABLE_PRECOMPILE_HEADERS ON UNITY_BUILD OFF)
    add_custom_target(cd_shared_architecture_checks
        COMMAND "${Python3_EXECUTABLE}" "${cd_architecture_root}/tests/architecture/check_negative.py"
            "$<TARGET_FILE:cd_ambience_lifecycle_negative>"
        COMMAND "$<TARGET_FILE:cd_themed_widget_tests>"
        COMMAND "$<TARGET_FILE:cd_themed_productivity_tests>"
        COMMAND "$<TARGET_FILE:cd_themed_monitor_tests>"
        COMMAND "$<TARGET_FILE:cd_media_controller_tests>"
        COMMAND "$<TARGET_FILE:cd_themed_ambience_tests>"
        DEPENDS cd_public_structure_check cd_shared_header_checks cd_ambience_lifecycle_negative
            cd_themed_widget_tests cd_themed_productivity_tests cd_themed_monitor_tests
            cd_media_controller_tests cd_themed_ambience_tests
        VERBATIM)
endif()
