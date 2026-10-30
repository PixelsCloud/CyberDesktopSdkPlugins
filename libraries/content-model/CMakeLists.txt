cmake_minimum_required(VERSION 3.21)
if(TARGET cd_content_text)
    return()
endif()
# vcpkg's static zlib is named zs.lib; CMake 3.28's FindZLIB omits that name
# when this public tree is consumed through CMAKE_PREFIX_PATH without a toolchain.
find_library(ZLIB_LIBRARY_RELEASE NAMES zs zlibstatic zlib z REQUIRED)
find_package(CURL CONFIG REQUIRED)
get_target_property(curl_type CURL::libcurl_static TYPE)
if(NOT curl_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Content URL helpers require static libcurl for the /MT plugin build")
endif()
add_library(cd_content_text STATIC Text.cpp)
target_include_directories(cd_content_text PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(cd_content_text PRIVATE CURL::libcurl_static)
target_compile_definitions(cd_content_text PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(cd_content_text PRIVATE /utf-8 /W4)
set_target_properties(cd_content_text PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
