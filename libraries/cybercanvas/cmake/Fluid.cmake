find_program(CYBERCANVAS_CARGO cargo REQUIRED)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/salva/provenance.json" fluid_provenance)
string(JSON fluid_notices_lock GET "${fluid_provenance}" lockSha256)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/fluid-backend/Cargo.lock" fluid_current_lock)
if(NOT fluid_notices_lock STREQUAL fluid_current_lock)
  message(FATAL_ERROR "Fluid license inventory is stale; run scripts/update-fluid-licenses.ps1")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR "Salva fluid source builds currently require /MT Release or RelWithDebInfo")
endif()
set(fluid_backend_dir "${CMAKE_CURRENT_BINARY_DIR}/fluid-target")
set(fluid_backend_lib "${fluid_backend_dir}/x86_64-pc-windows-msvc/release/cybercanvas_fluid_backend.lib")
file(GLOB_RECURSE fluid_backend_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fluid-backend/src/*.rs")
add_custom_command(OUTPUT "${fluid_backend_lib}"
  COMMAND ${CMAKE_COMMAND} -E env "RUSTFLAGS=-C target-feature=+crt-static" "CARGO_BUILD_JOBS=1"
    "${CYBERCANVAS_CARGO}" build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/fluid-backend/Cargo.toml"
    --locked --release --target x86_64-pc-windows-msvc --target-dir "${fluid_backend_dir}" --jobs 1
  DEPENDS ${fluid_backend_sources} "${CMAKE_CURRENT_SOURCE_DIR}/fluid-backend/Cargo.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/fluid-backend/Cargo.lock" VERBATIM)
add_custom_target(cybercanvas_fluid_backend_build DEPENDS "${fluid_backend_lib}")
add_library(cybercanvas_fluid STATIC src/Fluid.cpp)
add_library(CyberCanvas::Fluid ALIAS cybercanvas_fluid)
add_dependencies(cybercanvas_fluid cybercanvas_fluid_backend_build)
set_target_properties(cybercanvas_fluid PROPERTIES EXPORT_NAME Fluid AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(cybercanvas_fluid PUBLIC CyberCanvas::Particles PRIVATE
  $<BUILD_INTERFACE:${fluid_backend_lib}> $<INSTALL_INTERFACE:CyberCanvas::FluidBackend>
  ws2_32 userenv bcrypt ntdll advapi32)
target_compile_options(cybercanvas_fluid PRIVATE /W4 /utf-8)
install(TARGETS cybercanvas_fluid EXPORT CyberCanvasTargets)
install(FILES "${fluid_backend_lib}" DESTINATION ${CMAKE_INSTALL_LIBDIR} RENAME cybercanvas-fluid-backend.lib)
install(DIRECTORY third_party/salva/ DESTINATION ${CMAKE_INSTALL_DATADIR}/cybercanvas/third_party/salva)
install(DIRECTORY fluid-backend/ DESTINATION ${CMAKE_INSTALL_DATADIR}/cybercanvas/fluid-backend
  PATTERN "target" EXCLUDE)
if(CYBERCANVAS_BUILD_PHYSICS)
  target_link_libraries(cybercanvas_fluid PUBLIC CyberCanvas::Physics2D)
  target_compile_definitions(cybercanvas_fluid PUBLIC CYBERCANVAS_FLUID_BOX2D=1)
endif()
add_executable(cybercanvas_fluid_tests EXCLUDE_FROM_ALL tests/FluidTest.cpp)
target_link_libraries(cybercanvas_fluid_tests PRIVATE CyberCanvas::Fluid)
set_target_properties(cybercanvas_fluid_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(PROJECT_IS_TOP_LEVEL)
  add_test(NAME cybercanvas_fluid COMMAND cybercanvas_fluid_tests)
endif()
find_package(nlohmann_json CONFIG QUIET)
if(CYBERCANVAS_BUILD_PHYSICS AND TARGET nlohmann_json::nlohmann_json)
  add_executable(cybercanvas_particle_effects_demo EXCLUDE_FROM_ALL examples/ParticleEffects.cpp)
  target_link_libraries(cybercanvas_particle_effects_demo PRIVATE CyberCanvas::Fluid CyberCanvas::PhysicsParticles
    nlohmann_json::nlohmann_json)
  target_compile_options(cybercanvas_particle_effects_demo PRIVATE /W4 /utf-8)
  target_compile_definitions(cybercanvas_particle_effects_demo PRIVATE
    CYBERCANVAS_PARTICLE_TEMPLATE="${CMAKE_CURRENT_SOURCE_DIR}/examples/ParticleEffects.html"
    CYBERCANVAS_PARTICLE_ICONS="${CMAKE_CURRENT_SOURCE_DIR}/../cyberglass/resources/lucide")
  set_target_properties(cybercanvas_particle_effects_demo PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  install(FILES examples/ParticleEffects.cpp examples/ParticleEffects.html DESTINATION ${CMAKE_INSTALL_DATADIR}/cybercanvas/examples)
  foreach(particle_icon play pause rotate-ccw maximize x)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../cyberglass/resources/lucide/${particle_icon}.svg"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/cybercanvas/examples/particle-effects-icons)
  endforeach()
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../cyberglass/resources/lucide/LICENSE.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/cybercanvas/examples/particle-effects-icons)
endif()
