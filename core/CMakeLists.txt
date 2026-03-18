set(TERMCORE_SOURCES
    src/termcore_api.cpp
    src/vt_parser.cpp
    src/screen.cpp
    src/screen_csi.cpp
    src/screen_csi_ext.cpp
    src/screen_osc.cpp
    src/mouse.cpp
    src/font/unicode_width.cpp
    src/font/box_drawing.cpp
    src/font/box_drawing_lines.cpp
    src/font/font_shaper.cpp
    src/font/glyph_atlas.cpp
    src/font/glyph_cache.cpp
    src/font/font_collection.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOURCES src/pty_unix.cpp)
endif()

# ICU for Unicode properties
set(ICU_ROOT "/opt/homebrew/opt/icu4c@77")
list(APPEND CMAKE_PREFIX_PATH "${ICU_ROOT}")
find_package(ICU REQUIRED COMPONENTS uc)

# HarfBuzz for text shaping
find_package(PkgConfig REQUIRED)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)

add_library(termcore STATIC ${TERMCORE_SOURCES})

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(termcore PUBLIC ICU::uc ${HARFBUZZ_LIBRARIES})
target_link_directories(termcore PUBLIC ${HARFBUZZ_LIBRARY_DIRS})
