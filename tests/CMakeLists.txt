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
    test_url_highlight.cpp
    test_kitty_keyboard.cpp
    test_sixel.cpp
    test_sixel_animation.cpp
    test_kitty_graphics.cpp
    test_iterm_image.cpp
    test_image_preview.cpp
    test_shell_integration.cpp
    test_variable_font.cpp
    test_color_font.cpp
    test_bitmap_flip.cpp
    test_terminfo.cpp
    test_paste_guard.cpp
    test_session.cpp
    test_git_branch_detector.cpp
    test_workspace_status.cpp
    test_ssh.cpp
    test_jsonrpc.cpp
    test_command_dispatcher.cpp
    test_mcp_agent_commands.cpp
    test_terminal_inspector.cpp
    test_serial_port.cpp
    test_theme_loader.cpp
    test_theme_index.cpp
    test_theme_importer.cpp
    test_theme_repository.cpp
    test_theme_preview.cpp
    test_clipboard_history.cpp
    test_font_index.cpp
    test_profile.cpp
    test_shell_detector.cpp
    test_update_checker.cpp
    test_ssh.cpp
    test_ssh_mux.cpp
    test_ssh_transport.cpp
    test_accessibility.cpp
    test_grapheme.cpp
    test_ligature.cpp
    test_url_highlight.cpp
    test_session_autosave.cpp
    test_shader_effect.cpp
    test_port_detector.cpp
    test_instant_replay.cpp
    test_annotations.cpp
    test_password_manager.cpp
    test_tmux_integration.cpp
    test_subpixel.cpp
    test_screen_export.cpp
    test_lua_plugin_integration.cpp
    test_lua_defaults.cpp
    test_agent_tree_tracker.cpp
    test_hook_bridge.cpp
    test_osc_7770.cpp
    test_provider_registry.cpp
    test_sidebar_model.cpp
    test_osc_emitter.cpp
)

target_link_libraries(termcore_tests
    PRIVATE
        termcore
        GTest::gtest_main
)

target_include_directories(termcore_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/core/src
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

if(UNIX AND NOT APPLE)
    target_sources(termcore_tests PRIVATE
        test_wayland.cpp
    )
endif()

include(GoogleTest)
gtest_discover_tests(termcore_tests)

# Bread CLI tests
add_executable(bread_tmux_compat_test
    test_tmux_compat.cpp
    ${CMAKE_SOURCE_DIR}/tools/bread/tmux_compat.cpp
    ${CMAKE_SOURCE_DIR}/tools/bread/arg_parser.cpp
)
target_compile_features(bread_tmux_compat_test PRIVATE cxx_std_20)
target_include_directories(bread_tmux_compat_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/bread
)
target_link_libraries(bread_tmux_compat_test PRIVATE nlohmann_json::nlohmann_json GTest::gtest_main)
gtest_discover_tests(bread_tmux_compat_test)
