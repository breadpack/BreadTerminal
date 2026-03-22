include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

FetchContent_MakeAvailable(googletest)

add_executable(termcore_tests
    test_placeholder.cpp
    test_vt_parser.cpp
    test_screen.cpp
    test_screen_modes.cpp
    test_screen_osc.cpp
    test_screen_osc_colors.cpp
    test_screen_csi_ext.cpp
    test_pty.cpp
    test_capi.cpp
    test_unicode_width.cpp
    test_box_drawing.cpp
    test_font_shaper.cpp
    test_glyph_atlas.cpp
    test_glyph_cache.cpp
    test_font_collection.cpp
    test_mouse.cpp
    test_mux.cpp
    test_lua_engine.cpp
    test_notification.cpp
    test_config.cpp
    test_config_diff.cpp
    test_agent.cpp
    test_keybinding.cpp
    test_search.cpp
    test_search_history.cpp
    test_url_detector.cpp
    test_kitty_keyboard.cpp
    test_sixel.cpp
    test_kitty_graphics.cpp
    test_iterm_image.cpp
    test_shell_integration.cpp
    test_variable_font.cpp
    test_bitmap_flip.cpp
    test_terminfo.cpp
    test_paste_guard.cpp
    test_session.cpp
    test_git_branch_detector.cpp
    test_workspace_status.cpp
    test_jsonrpc.cpp
    test_command_dispatcher.cpp
    test_mcp_agent_commands.cpp
    test_theme_loader.cpp
    test_theme_index.cpp
    test_theme_importer.cpp
    test_theme_repository.cpp
    test_theme_preview.cpp
    test_font_index.cpp
    test_profile.cpp
    test_shell_detector.cpp
    test_update_checker.cpp
    test_ssh.cpp
    test_accessibility.cpp
)

target_link_libraries(termcore_tests
    PRIVATE
        termcore
        GTest::gtest_main
)

if(WIN32)
    target_link_libraries(termcore_tests PRIVATE termcore_windows)
endif()

if(APPLE)
    target_sources(termcore_tests PRIVATE
        test_coretext_rasterizer.cpp
        test_coretext_discovery.cpp
        test_metal_renderer.mm
        test_webview.cpp
    )
    target_link_libraries(termcore_tests PRIVATE termcore_macos)
    set_source_files_properties(
        test_metal_renderer.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
endif()

include(GoogleTest)
gtest_discover_tests(termcore_tests)
