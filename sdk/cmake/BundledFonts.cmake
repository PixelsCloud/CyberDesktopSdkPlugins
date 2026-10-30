# Fonts used by the SDK's default font IDs. Keep the original notices adjacent
# to the unchanged font files. System fonts and user-selected fonts are external.
set(cd_sdk_font_root "${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts")
foreach(family SiYuanZhenHei-Heavy ZQKFreeFont)
    if(NOT IS_DIRECTORY "${cd_sdk_font_root}/${family}")
        message(FATAL_ERROR "Missing default font assets: ${cd_sdk_font_root}/${family}")
    endif()
    file(GLOB font_files CONFIGURE_DEPENDS "${cd_sdk_font_root}/${family}/*")
    foreach(font IN LISTS font_files)
        get_filename_component(name "${font}" NAME)
        configure_file("${font}" "${CMAKE_BINARY_DIR}/assets/fonts/${family}/${name}" COPYONLY)
    endforeach()
    install(DIRECTORY "${cd_sdk_font_root}/${family}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/assets/fonts")
endforeach()
