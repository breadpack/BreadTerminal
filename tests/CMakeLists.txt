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
    test_pty.cpp
    test_capi.cpp
    test_unicode_width.cpp
    test_box_drawing.cpp
    test_font_shaper.cpp
    test_glyph_atlas.cpp
)

target_link_libraries(termcore_tests
    PRIVATE
        termcore
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(termcore_tests)
