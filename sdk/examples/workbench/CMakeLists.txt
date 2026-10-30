cyberdesktop_add_plugin(cd_sdk_example_workbench ID org.cyberdesktop.sdk.workbench
  DESCRIPTOR "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto"
  SOURCES ../Workbench.cpp ../../templates/components/Card.cpp ../../templates/components/ServiceModel.cpp ASSETS art.png)

target_include_directories(cd_sdk_example_workbench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
