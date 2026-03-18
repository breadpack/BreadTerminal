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
    src/font/variable_font.cpp
    src/mux.cpp
    src/lua_engine.cpp
    src/notification.cpp
    src/config.cpp
    src/config_themes.cpp
    src/agent.cpp
    src/keybinding.cpp
    src/search.cpp
    src/url_detector.cpp
    src/kitty_keyboard.cpp
    src/sixel.cpp
    src/kitty_graphics.cpp
    src/shell_integration.cpp
    src/webview_stub.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOURCES src/pty_unix.cpp)
endif()

if(WIN32)
    list(APPEND TERMCORE_SOURCES src/pty_windows.cpp)
endif()

# ICU for Unicode properties
if(NOT ICU_ROOT AND APPLE AND EXISTS "/opt/homebrew/opt/icu4c@77")
    set(ICU_ROOT "/opt/homebrew/opt/icu4c@77")
endif()
if(ICU_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${ICU_ROOT}")
endif()
find_package(ICU REQUIRED COMPONENTS uc)

# HarfBuzz for text shaping
if(WIN32)
    find_package(harfbuzz CONFIG REQUIRED)
    set(HARFBUZZ_LIBRARIES harfbuzz::harfbuzz)
    set(HARFBUZZ_INCLUDE_DIRS "")
    set(HARFBUZZ_LIBRARY_DIRS "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
endif()

# Lua 5.4
if(WIN32)
    find_package(Lua REQUIRED)
    set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
    set(LUA_LIBRARY_DIRS "")
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/opt/homebrew/opt/lua@5.4/lib/pkgconfig")
    pkg_check_modules(LUA REQUIRED lua5.4)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA REQUIRED lua5.4)
endif()

# Sol3 (header-only Lua binding)
include(FetchContent)
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG develop
)
FetchContent_Populate(sol2)
add_library(sol2_header_only INTERFACE)
target_include_directories(sol2_header_only INTERFACE ${sol2_SOURCE_DIR}/include)

add_library(termcore STATIC ${TERMCORE_SOURCES})

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HARFBUZZ_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIRS}
)

target_link_libraries(termcore PUBLIC ICU::uc ${HARFBUZZ_LIBRARIES} sol2_header_only ${LUA_LIBRARIES})
target_link_directories(termcore PUBLIC ${HARFBUZZ_LIBRARY_DIRS} ${LUA_LIBRARY_DIRS})
