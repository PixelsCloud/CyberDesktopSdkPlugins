cyberdesktop_add_plugin(cd_sdk_example_physics_sandbox ID org.cyberdesktop.sdk.physics-sandbox
  DESCRIPTOR "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto" SOURCES ../PhysicsSandbox.cpp)
target_link_libraries(cd_sdk_example_physics_sandbox PRIVATE CyberCanvas::PhysicsScene)
