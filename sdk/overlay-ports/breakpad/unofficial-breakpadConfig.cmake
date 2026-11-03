if(@USED_ZLIB@)
  include(CMakeFindDependencyMacro)
  find_dependency(ZLIB)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/unofficial-breakpadTargets.cmake")

set(CYBER_BREAKPAD_GRACEFUL_SHUTDOWN TRUE)
