# nlohmann/json for session serialization
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

# zlib for scrollback compression
find_package(ZLIB REQUIRED)

set(TERMCORE_SOURCES
    src/termcore_api.cpp
    src/vt_parser.cpp
    src/dynamic_colors.cpp
    src/contrast.cpp
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
    src/mux_layout.cpp
    src/notification.cpp
    src/notification_visual.cpp
    src/config.cpp
    src/config_diff.cpp
    src/ghostty_config_import.cpp
    src/config_themes.cpp
    src/theme_loader.cpp
    src/theme_index.cpp
    src/font_index.cpp
    src/agent.cpp
    src/agent_orchestrator.cpp
    src/keybinding.cpp
    src/git_branch_detector.cpp
    src/port_detector.cpp
    src/pr_detector.cpp
    src/workspace_status.cpp
    src/search.cpp
    src/url_detector.cpp
    src/kitty_keyboard.cpp
    src/sixel.cpp
    src/kitty_graphics.cpp
    src/shell_integration.cpp
    src/paste_guard.cpp
    src/vi_copy_mode.cpp
    src/webview_stub.cpp
    src/terminfo.cpp
    src/session_paths.cpp
    src/session_serializer.cpp
    src/session_deserializer.cpp
    src/pane_status.cpp
    src/session_manager.cpp
    src/socket/jsonrpc.cpp
    src/socket/command_dispatcher_mux.cpp
    src/socket/command_dispatcher_misc.cpp
    src/socket/command_dispatcher_browser.cpp
    src/socket/command_dispatcher_agent.cpp
    src/socket/command_dispatcher_orchestrator.cpp
    src/socket/socket_server.cpp
    src/screen_capture.cpp
    src/mcp_server_spec.cpp
    src/terminal_controller.cpp
    src/input_handler.cpp
    src/font_manager.cpp
    src/selection_manager.cpp
    src/search_controller.cpp
    src/tab_controller.cpp
    src/config_applier.cpp
    src/settings_model.cpp
    src/config_value_adapter.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOURCES
        src/pty_unix.cpp
        src/socket/unix_socket_transport.cpp
    )
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

# Lua 5.4 (optional — disabled if not found or incompatible)
set(TERMCORE_HAS_LUA OFF)
if(WIN32)
    find_package(Lua QUIET)
    if(LUA_FOUND OR (LUA_INCLUDE_DIR AND LUA_LIBRARY))
        # Accept externally provided LUA_INCLUDE_DIR / LUA_LIBRARY
        if(NOT LUA_VERSION_STRING)
            set(LUA_VERSION_STRING "5.4")
        endif()
        if(LUA_VERSION_STRING VERSION_LESS "5.5")
            set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
            set(LUA_LIBRARIES ${LUA_LIBRARY})
            set(LUA_LIBRARY_DIRS "")
            set(TERMCORE_HAS_LUA ON)
        endif()
    endif()
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/opt/homebrew/opt/lua@5.4/lib/pkgconfig")
    pkg_check_modules(LUA lua5.4)
    if(LUA_FOUND)
        set(TERMCORE_HAS_LUA ON)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUA lua5.4)
    if(NOT LUA_FOUND)
        pkg_check_modules(LUA lua)
    endif()
    if(LUA_FOUND)
        set(TERMCORE_HAS_LUA ON)
    endif()
endif()

if(TERMCORE_HAS_LUA)
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
    list(APPEND TERMCORE_SOURCES src/lua_engine.cpp)
    message(STATUS "Lua plugin system: ENABLED (${LUA_VERSION})")
else()
    message(FATAL_ERROR "Lua 5.4 is required but not found. Install lua5.4 and liblua5.4-dev.")
endif()

# lua_config.cpp has both TERMCORE_HAS_LUA and stub paths, always compile it
# lua_config_writer.cpp has no Lua runtime dependency — always compile it
list(APPEND TERMCORE_SOURCES src/lua_config.cpp src/lua_config_writer.cpp)

add_library(termcore STATIC ${TERMCORE_SOURCES})

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(termcore PUBLIC ICU::uc ${HARFBUZZ_LIBRARIES} nlohmann_json::nlohmann_json ZLIB::ZLIB)
target_link_directories(termcore PUBLIC ${HARFBUZZ_LIBRARY_DIRS})

if(TERMCORE_HAS_LUA)
    target_include_directories(termcore PUBLIC ${LUA_INCLUDE_DIRS})
    target_link_libraries(termcore PUBLIC sol2_header_only ${LUA_LIBRARIES})
    target_link_directories(termcore PUBLIC ${LUA_LIBRARY_DIRS})
    target_compile_definitions(termcore PUBLIC TERMCORE_HAS_LUA=1)
endif()
