# Ninja matches /showIncludes against raw compiler bytes. CMake 3.28's AUTO
# decoding can transcode a localized prefix, silently recording zero headers.
# Probe without transcoding so incremental builds retain their dependencies.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
  set(_cd_public_show_includes_dir "${CMAKE_BINARY_DIR}/CMakeFiles/ShowIncludes")
  file(MAKE_DIRECTORY "${_cd_public_show_includes_dir}")
  file(WRITE "${_cd_public_show_includes_dir}/foo.h" "\n")
  file(WRITE "${_cd_public_show_includes_dir}/main.c" "#include \"foo.h\"\nint main(void) { return 0; }\n")
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c main.c
    WORKING_DIRECTORY "${_cd_public_show_includes_dir}"
    OUTPUT_VARIABLE _cd_public_show_includes_output
    ERROR_VARIABLE _cd_public_show_includes_error
    RESULT_VARIABLE _cd_public_show_includes_result
    ENCODING NONE)
  if(_cd_public_show_includes_result EQUAL 0 AND
     "${_cd_public_show_includes_output}" MATCHES "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|/)")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
  else()
    message(FATAL_ERROR "Cannot detect the raw MSVC include prefix: ${_cd_public_show_includes_error}")
  endif()
  unset(_cd_public_show_includes_dir)
  unset(_cd_public_show_includes_output)
  unset(_cd_public_show_includes_error)
  unset(_cd_public_show_includes_result)
endif()

