cyberdesktop_add_plugin(cd_sdk_example_rendering_gallery ID org.cyberdesktop.sdk.rendering-gallery
  DESCRIPTOR "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto" SOURCES ../RenderingGallery.cpp ASSETS orbit.json)
target_link_libraries(cd_sdk_example_rendering_gallery PRIVATE CyberCanvas::Vector CyberCanvas::Text CyberCanvas::Charts CyberCanvas::Particles)
