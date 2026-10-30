cmake_minimum_required(VERSION 3.26)
project(CyberDesktopSdkPlugins LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/NativeCompilerDependencies.cmake")

# This file becomes CMakeLists.txt at the root of the public candidate tree.
# SDK-only, installed-SDK consumption and full source development share the
# plugin entry and its explicit family/ID dependency inventory.
set(CD_PLUGINS_SDK_MODE SOURCE CACHE STRING "SDK source or installed kit")
add_subdirectory(plugins)
