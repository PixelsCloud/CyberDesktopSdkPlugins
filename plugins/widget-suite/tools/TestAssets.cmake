# The widget regression also exercises mechanical skins. Stage only those public
# images beside the test executable; do not depend on a previously deployed app.
set(widget_test_art "${CMAKE_CURRENT_LIST_DIR}/../../mechanical-instruments/assets")
set(widget_test_recipes)
foreach(size 1x4 2x2 2x4)
    set(folder "player-${size}")
    if(size STREQUAL "2x4")
        set(folder player)
    endif()
    set(destination "$<TARGET_FILE_DIR:cd_widget_tests>/plugins/com.cyberdesktop.music.player.mechanical.${size}/assets/mechanical")
    list(APPEND widget_test_recipes
        COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${widget_test_art}/${folder}" "${destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${widget_test_art}/player/arm.png" "${widget_test_art}/player/vinyl.png" "${destination}")
endforeach()
set(destination "$<TARGET_FILE_DIR:cd_widget_tests>/plugins/com.cyberdesktop.album.mechanical.2x4/assets/mechanical")
list(APPEND widget_test_recipes
    COMMAND ${CMAKE_COMMAND} -E make_directory "${destination}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${widget_test_art}/album-2x4" "${destination}")
cd_asset_target(cd_widget_test_assets ${widget_test_recipes} VERBATIM)
add_dependencies(cd_widget_tests cd_widget_test_assets)
