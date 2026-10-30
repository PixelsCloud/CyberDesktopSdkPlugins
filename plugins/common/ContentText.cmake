include_guard(GLOBAL)
if(NOT TARGET cd_content_text)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../libraries/content-model" "${CMAKE_BINARY_DIR}/content-model")
endif()
