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
    test_screen_csi_ext.cpp
    test_pty.cpp
    test_capi.cpp
    test_unicode_width.cpp
    test_box_drawing.cpp
    test_font_shaper.cpp
    test_glyph_atlas.cpp
    test_glyph_cache.cpp
    test_font_collection.cpp
)

target_link_libraries(termcore_tests
    PRIVATE
        termcore
        GTest::gtest_main
)

if(APPLE)
    target_sources(termcore_tests PRIVATE
        test_coretext_rasterizer.cpp
        test_coretext_discovery.cpp
        test_metal_renderer.mm
    )
    target_link_libraries(termcore_tests PRIVATE termcore_macos)
    set_source_files_properties(
        test_metal_renderer.mm
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
endif()

include(GoogleTest)
gtest_discover_tests(termcore_tests)
