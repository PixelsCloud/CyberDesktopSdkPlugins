cmake_minimum_required(VERSION 3.26)
project(CyberDwmGlass LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native DWM adapter requires Windows x64 / MSVC")
endif()
if(NOT TARGET CyberGlass::Liquid)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../liquidglass" "${CMAKE_CURRENT_BINARY_DIR}/liquidglass")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(wil CONFIG REQUIRED)
find_library(cd_slim_detours NAMES KNSoft.SlimDetours REQUIRED)
get_filename_component(cd_public_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(cd_openglass_root "${cd_public_root}/third_party/OpenGlass")
if(NOT EXISTS "${cd_openglass_root}/OpenGlass/ProjectionHelper.hpp")
    message(FATAL_ERROR "Initialize the pinned third_party/OpenGlass Git submodule")
endif()
set(cd_openglass_source "${cd_openglass_root}/OpenGlass")
set(cd_projection_output "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CYBERGLASS_PROJECTION_PYTHON_PATH "${CMAKE_BINARY_DIR}/Tools/openglass-python" CACHE PATH
    "Directory containing the pinned OpenGlass projection generator's Python dependencies")
set(cd_projection_outputs)
foreach(cd_file IN ITEMS udwm.Layouts.generated.hpp udwm.Symbols.generated.hpp dwmcore.Layouts.generated.hpp
        dwmcore.Symbols.generated.hpp ProjectionRegistry.generated.inc SymbolCatalog.generated.hpp SymbolCatalog.generated.inc)
    list(APPEND cd_projection_outputs "${cd_projection_output}/${cd_file}")
endforeach()
file(GLOB_RECURSE cd_projection_inputs CONFIGURE_DEPENDS
    "${cd_openglass_source}/ProjectionSchemas/legacy/*.json"
    "${cd_openglass_source}/SymbolCatalogs/legacy/*.json" "${cd_openglass_root}/tools/*.py")
add_custom_command(OUTPUT ${cd_projection_outputs}
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${CYBERGLASS_PROJECTION_PYTHON_PATH}"
        "${Python3_EXECUTABLE}" -m tools.projection_codegen --repo "${cd_openglass_root}"
        --architecture legacy --platform x64 --output "${cd_projection_output}"
    WORKING_DIRECTORY "${cd_openglass_root}" DEPENDS ${cd_projection_inputs} VERBATIM)
add_custom_target(cyber_dwm_projection DEPENDS ${cd_projection_outputs})
set(cd_openglass_units AeroColorizationEffect AeroEffect BlurEffect BuiltInSymbolCatalog
    D3DGlassRealizer GlassCoverageSet GlassKernel GlassRealizer GlassSafetyZoneLayer MaterialRealizer
    ProjectionRegistry ProjectionRuntime SymbolCatalog SymbolParser)
set(cd_openglass_sources)
foreach(cd_unit IN LISTS cd_openglass_units)
    list(APPEND cd_openglass_sources "${cd_openglass_source}/${cd_unit}.cpp")
endforeach()
list(APPEND cd_openglass_sources
    "${cd_openglass_source}/Architecture/Legacy/GlassIntegrity.cpp"
    "${cd_openglass_source}/Architecture/Legacy/GlassRenderer.cpp"
    "${cd_openglass_source}/Architecture/Legacy/GlassReflectionHandler.cpp"
    "${cd_openglass_source}/Architecture/Legacy/GlassReflectionBrush.cpp"
    "${cd_openglass_source}/Architecture/Legacy/ReflectionRealizer.cpp"
    "${cd_openglass_root}/Common/HookHelper.cpp" "${cd_openglass_root}/Common/PngAssetValidation.cpp"
    "${cd_openglass_root}/Common/CustomBlurEffect.cpp")
add_library(cyber_openglass_identity STATIC EXCLUDE_FROM_ALL "${cd_openglass_root}/Common/PeCodeViewIdentity.cpp")
target_compile_features(cyber_openglass_identity PRIVATE cxx_std_20)
target_link_libraries(cyber_openglass_identity PRIVATE WIL::WIL)
target_compile_options(cyber_openglass_identity PRIVATE /utf-8 /W4)
target_compile_definitions(cyber_openglass_identity PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_definitions(cyber_openglass_identity PUBLIC WIL_LOCK_EXCEPTION_MODE WIL_USE_STL=1)
set_target_properties(cyber_openglass_identity PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(cyber_openglass_engine STATIC EXCLUDE_FROM_ALL ${cd_openglass_sources})
add_dependencies(cyber_openglass_engine cyber_dwm_projection)
set(cd_engine_includes "${cd_projection_output}" "${cd_openglass_source}/Architecture/Legacy"
    "${cd_openglass_source}" "${cd_openglass_root}/Common")
target_include_directories(cyber_openglass_engine PRIVATE ${cd_engine_includes})
target_compile_definitions(cyber_openglass_engine PRIVATE OPENGLASS_DWM_LEGACY=1 WIL_LOCK_EXCEPTION_MODE WIL_USE_STL=1
    NOMINMAX UNICODE _UNICODE)
target_compile_options(cyber_openglass_engine PRIVATE /utf-8 /W4 /Gy /Gw)
target_link_libraries(cyber_openglass_engine PRIVATE cyber_openglass_identity WIL::WIL "${cd_slim_detours}")
set_target_properties(cyber_openglass_engine PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_program(cd_fxc NAMES fxc.exe HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64" REQUIRED)
find_path(cd_d2d_shader_include NAMES d2d1effecthelpers.hlsli
    HINTS "$ENV{WindowsSdkDir}/Include/$ENV{WindowsSDKVersion}/um" REQUIRED)
set(cd_shader_outputs)
foreach(cd_shader IN ITEMS AeroColorizationEffect Blur.VS Blur.PS_BlurH Blur.PS_BlurV Blur.PS_BlurV_Vista)
    if(cd_shader STREQUAL "AeroColorizationEffect")
        set(cd_shader_entry AeroColorizationEffect)
        set(cd_shader_profile ps_5_0)
        set(cd_shader_flags /D D2D_FULL_SHADER /D D2D_ENTRY=AeroColorizationEffect)
    else()
        set(cd_shader_entry main)
        set(cd_shader_flags)
        if(cd_shader STREQUAL "Blur.VS")
            set(cd_shader_profile vs_5_0)
        else()
            set(cd_shader_profile ps_5_0)
        endif()
    endif()
    set(cd_shader_output "${cd_projection_output}/${cd_shader}.cso")
    add_custom_command(OUTPUT "${cd_shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${cd_projection_output}"
        COMMAND "${cd_fxc}" /nologo /O3 /I "${cd_d2d_shader_include}" /T "${cd_shader_profile}" /E "${cd_shader_entry}" ${cd_shader_flags}
            /Fo "${cd_shader_output}" "${cd_openglass_source}/${cd_shader}.hlsl"
        DEPENDS "${cd_openglass_source}/${cd_shader}.hlsl" "${cd_openglass_source}/Common.hlsli"
            "${cd_openglass_source}/Blur.Common.hlsli" VERBATIM)
    list(APPEND cd_shader_outputs "${cd_shader_output}")
endforeach()
add_custom_target(cyber_dwm_native_shaders DEPENDS ${cd_shader_outputs})
configure_file(resources/NativeShaders.rc.in "${CMAKE_CURRENT_BINARY_DIR}/NativeShaders.rc" @ONLY)
add_library(cyber_glass_dwm SHARED EXCLUDE_FROM_ALL
    src/DwmEngine.cpp src/DwmSurfaceRegistry.cpp src/DwmMaterial.cpp src/DwmInfrastructure.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/NativeShaders.rc")
add_dependencies(cyber_glass_dwm cyber_dwm_projection cyber_dwm_native_shaders)
target_include_directories(cyber_glass_dwm PRIVATE "${cd_public_root}" ${cd_engine_includes})
target_compile_definitions(cyber_glass_dwm PRIVATE OPENGLASS_DWM_LEGACY=1 WIL_LOCK_EXCEPTION_MODE WIL_USE_STL=1
    NOMINMAX UNICODE _UNICODE)
target_compile_options(cyber_glass_dwm PRIVATE /utf-8 /W4 /Gy /Gw)
target_link_libraries(cyber_glass_dwm PRIVATE CyberGlass::Liquid cyber_openglass_engine WIL::WIL
    "${cd_slim_detours}" d2d1 d3d11 d3dcompiler dxgi dxguid dwmapi dbghelp windowsapp
    user32 gdi32 advapi32 shell32 shlwapi pathcch comctl32 ole32 oleaut32 uxtheme wtsapi32 ntdll powrprof shcore version)
target_link_options(cyber_glass_dwm PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO)
set_target_properties(cyber_glass_dwm PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(cpr CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS asio)
find_package(spdlog CONFIG REQUIRED)
add_executable(cyber_glass_host EXCLUDE_FROM_ALL host/main.cpp host/NativeHost.cpp host/DwmAttachment.cpp host/DwmSymbols.cpp)
target_include_directories(cyber_glass_host PRIVATE "${cd_public_root}")
target_compile_features(cyber_glass_host PRIVATE cxx_std_20)
target_compile_definitions(cyber_glass_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
    SPDLOG_WCHAR_FILENAMES)
target_compile_options(cyber_glass_host PRIVATE /utf-8 /W4 /Gy /Gw)
target_link_libraries(cyber_glass_host PRIVATE cyber_openglass_identity WIL::WIL Boost::asio
    cpr::cpr spdlog::spdlog_header_only dbghelp windowsapp advapi32 wtsapi32 shlwapi shell32 ole32 bcrypt)
target_link_options(cyber_glass_host PRIVATE /OPT:REF /OPT:ICF /INCREMENTAL:NO)
set_target_properties(cyber_glass_host PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
