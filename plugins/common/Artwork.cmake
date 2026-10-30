include_guard(GLOBAL)

# Shared mechanical drawing implementation; no plugin identity or controller state.
add_library(cd_mechanical_art STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../mechanical-instruments/Artwork.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../mechanical-instruments/Icons.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../mechanical-instruments/InstrumentPlates.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../mechanical-instruments/PhysicsMaterials.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../mechanical-instruments/HangingParts.cpp")
target_link_libraries(cd_mechanical_art PUBLIC CyberDesktopSDK::SDK)
target_compile_options(cd_mechanical_art PRIVATE /utf-8 /W4)
set_target_properties(cd_mechanical_art PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(cd_mechanical_art PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../..")
