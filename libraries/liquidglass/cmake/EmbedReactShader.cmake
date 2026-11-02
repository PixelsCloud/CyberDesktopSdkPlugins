if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "Both INPUT and OUTPUT are required")
endif()
file(READ "${INPUT}" cd_shader)
get_filename_component(cd_shader_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${cd_shader_directory}")
file(WRITE "${OUTPUT}"
    "// Generated from src/react/Material.hlsl.\nconstexpr char kReactMaterialShader[] = R\"reactshader(${cd_shader})reactshader\";\n")
