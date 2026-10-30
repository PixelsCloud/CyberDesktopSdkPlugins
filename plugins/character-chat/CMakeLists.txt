cmake_minimum_required(VERSION 3.26)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CyberDesktopCharacterChat LANGUAGES CXX)
    find_package(CyberDesktopSDK 1 CONFIG REQUIRED)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../common/SdkTool.cmake")
cd_plugin_sdk_tool(cd_character_sdk_tool CD_CHARACTER_SDK_LICENSE)
find_package(nlohmann_json 3.12 CONFIG REQUIRED)
find_file(CD_CHARACTER_JSON_LICENSE NAMES copyright LICENSE.MIT
    HINTS "${nlohmann_json_DIR}" "${nlohmann_json_DIR}/../nlohmann-json" REQUIRED NO_DEFAULT_PATH)
if(NOT TARGET CyberDesktop::CharacterProtocol)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/character-protocol" "${CMAKE_CURRENT_BINARY_DIR}/protocol")
endif()
if(NOT TARGET cd_live2d_presentation)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../live2d-suite" "${CMAKE_CURRENT_BINARY_DIR}/live2d")
endif()
add_library(cd_character_chat_plugin MODULE Plugin.cpp)
target_link_libraries(cd_character_chat_plugin PRIVATE CyberDesktopSDK::SDK CyberDesktop::CharacterProtocol CyberCanvas::Text CyberCanvas::Layout)
target_compile_definitions(cd_character_chat_plugin PRIVATE CD_PLUGIN_BUILD)
target_compile_options(cd_character_chat_plugin PRIVATE /utf-8 /W4)
set_target_properties(cd_character_chat_plugin PROPERTIES PREFIX "" OUTPUT_NAME plugin
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.character-chat"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/com.cyberdesktop.character-chat"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/link"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
cd_asset_target(cd_character_chat_package
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses"
  COMMAND $<TARGET_FILE:cd_character_sdk_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/plugin.textproto"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/descriptor.pb"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/characters.toml" "$<TARGET_FILE_DIR:cd_character_chat_plugin>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/preview.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" "$<TARGET_FILE_DIR:cd_character_chat_plugin>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/USAGE.txt"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/README.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/schemas"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/schemas"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/sdk-preview.json"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/sdk-preview.json"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/asset-prompts.json"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/assets/asset-prompts.json"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-suite/models/mao"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/models/mao"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-suite/models/haru"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/models/haru"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-suite/licenses/Live2D-Free-Material-License.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-suite/licenses/Live2D-Sample-Model-Terms.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-suite/licenses/Live2D-SDK-LICENSE.md"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CHARACTER_SDK_LICENSE}"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses/CyberDesktopSDK-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/licenses/Lucide-ISC-MIT.txt"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses/Lucide-ISC-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:cd_character_chat_plugin>/offline-media.json"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/characters.json"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/media.json"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/assets/xiaqing/resources.json"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses/Whale-Girl-CC-BY-NC-SA-4.0.txt"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses/Whale-Girl-NOTICE.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_CHARACTER_JSON_LICENSE}"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses/nlohmann-json-MIT.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/character-protocol/LICENSE"
    "$<TARGET_FILE_DIR:cd_character_chat_plugin>/licenses/CharacterProtocol-MIT.txt"
  DEPENDS cd_character_chat_plugin cd_character_sdk_tool VERBATIM)


target_link_libraries(cd_character_chat_plugin PRIVATE cd_live2d_presentation)

target_include_directories(cd_character_chat_plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
cd_asset_target(cd_character_chat_archive_package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/character-chat-packages"
    COMMAND $<TARGET_FILE:cd_character_sdk_tool> pack "$<TARGET_FILE_DIR:cd_character_chat_plugin>"
        "${CMAKE_BINARY_DIR}/character-chat-packages/com.cyberdesktop.character-chat.cdpkg"
    DEPENDS cd_character_chat_package cd_character_sdk_tool VERBATIM)
