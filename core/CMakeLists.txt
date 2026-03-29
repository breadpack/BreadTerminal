# nlohmann/json for session serialization
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

# zlib for scrollback compression
find_package(ZLIB REQUIRED)

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

# libssh2 (optional — real SSH transport)
set(TERMCORE_HAS_LIBSSH2 OFF)
if(WIN32)
    find_package(Libssh2 CONFIG QUIET)
    if(Libssh2_FOUND)
        set(TERMCORE_HAS_LIBSSH2 ON)
    endif()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSSH2 libssh2)
        if(LIBSSH2_FOUND)
            set(TERMCORE_HAS_LIBSSH2 ON)
        endif()
    endif()
    if(NOT TERMCORE_HAS_LIBSSH2)
        find_package(Libssh2 CONFIG QUIET)
        if(Libssh2_FOUND)
            set(TERMCORE_HAS_LIBSSH2 ON)
        endif()
    endif()
endif()

# ===========================================================================
# Sub-library: termcore_font (no internal dependencies)
# ===========================================================================
set(TERMCORE_FONT_SOURCES
    src/font/unicode_width.cpp
    src/font/box_drawing.cpp
    src/font/box_drawing_lines.cpp
    src/font/font_shaper.cpp
    src/font/glyph_atlas.cpp
    src/font/glyph_cache.cpp
    src/font/raster_queue.cpp
    src/font/font_collection.cpp
    src/font/variable_font.cpp
    src/font/ligature.cpp
    src/font/color_font.cpp
    src/font/subpixel.cpp
    src/font_manager.cpp
    src/font_index.cpp
)

add_library(termcore_font STATIC ${TERMCORE_FONT_SOURCES})

target_include_directories(termcore_font
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(termcore_font PUBLIC
    ${HARFBUZZ_LIBRARIES} ICU::uc nlohmann_json::nlohmann_json
)
target_link_directories(termcore_font PUBLIC ${HARFBUZZ_LIBRARY_DIRS})

enable_sanitizers(termcore_font)

# ===========================================================================
# Sub-library: termcore_ssh (conditional libssh2)
# ===========================================================================
set(TERMCORE_SSH_SOURCES
    src/ssh_transport.cpp
    src/ssh_session.cpp
    src/ssh_mux.cpp
    src/ssh_mux_manager.cpp
    src/ssh_profile.cpp
    src/ssh_known_hosts.cpp
    src/ssh_terminfo.cpp
    src/ssh_integration_deploy.cpp
    src/ssh_transport_libssh2.cpp
    src/ssh_transport_libssh2_channel.cpp
)

add_library(termcore_ssh STATIC ${TERMCORE_SSH_SOURCES})

target_include_directories(termcore_ssh
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TERMCORE_HAS_LIBSSH2)
    target_compile_definitions(termcore_ssh PUBLIC TERMCORE_HAS_LIBSSH2=1)
    if(WIN32)
        target_link_libraries(termcore_ssh PUBLIC Libssh2::libssh2 ws2_32)
    elseif(TARGET Libssh2::libssh2)
        target_link_libraries(termcore_ssh PUBLIC Libssh2::libssh2)
    else()
        target_include_directories(termcore_ssh PUBLIC ${LIBSSH2_INCLUDE_DIRS})
        target_link_libraries(termcore_ssh PUBLIC ${LIBSSH2_LIBRARIES})
        target_link_directories(termcore_ssh PUBLIC ${LIBSSH2_LIBRARY_DIRS})
    endif()
    message(STATUS "SSH transport: ENABLED (libssh2)")
else()
    message(STATUS "SSH transport: STUB (libssh2 not found)")
endif()

enable_sanitizers(termcore_ssh)

# ===========================================================================
# Sub-library: termcore_image (image protocols)
# ===========================================================================
set(TERMCORE_IMAGE_SOURCES
    src/sixel.cpp
    src/sixel_animation.cpp
    src/kitty_graphics.cpp
    src/iterm_image.cpp
    src/image_preview.cpp
)

add_library(termcore_image STATIC ${TERMCORE_IMAGE_SOURCES})

target_include_directories(termcore_image
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_sanitizers(termcore_image)

# ===========================================================================
# Sub-library: termcore_shader (zero internal dependencies)
# ===========================================================================
set(TERMCORE_SHADER_SOURCES
    src/shader_effect.cpp
    src/shader_builtins.cpp
)

add_library(termcore_shader STATIC ${TERMCORE_SHADER_SOURCES})

target_include_directories(termcore_shader
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_sanitizers(termcore_shader)

# ===========================================================================
# Sub-library: termcore_theme (depends on config.h/profile.h headers only)
# ===========================================================================
set(TERMCORE_THEME_SOURCES
    src/theme_loader.cpp
    src/theme_index.cpp
    src/theme_importer.cpp
    src/theme_repository.cpp
    src/theme_preview.cpp
)

add_library(termcore_theme STATIC ${TERMCORE_THEME_SOURCES})

target_include_directories(termcore_theme
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(termcore_theme PUBLIC nlohmann_json::nlohmann_json)

enable_sanitizers(termcore_theme)

# ===========================================================================
# Sub-library: termcore_socket_transport (IPC transport + JSON-RPC protocol)
# ===========================================================================
set(TERMCORE_SOCKET_TRANSPORT_SOURCES
    src/socket/jsonrpc.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOCKET_TRANSPORT_SOURCES
        src/socket/unix_socket_transport.cpp
    )
endif()

if(WIN32)
    list(APPEND TERMCORE_SOCKET_TRANSPORT_SOURCES
        src/socket/win_pipe_transport.cpp
    )
endif()

add_library(termcore_socket_transport STATIC ${TERMCORE_SOCKET_TRANSPORT_SOURCES})

target_include_directories(termcore_socket_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(termcore_socket_transport PUBLIC nlohmann_json::nlohmann_json)

enable_sanitizers(termcore_socket_transport)

# ===========================================================================
# Sub-library: termcore_keybinding (zero internal dependencies)
# ===========================================================================
set(TERMCORE_KEYBINDING_SOURCES
    src/keybinding.cpp
    src/keybinding_presets.cpp
)

add_library(termcore_keybinding STATIC ${TERMCORE_KEYBINDING_SOURCES})

target_include_directories(termcore_keybinding
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_sanitizers(termcore_keybinding)

# ===========================================================================
# Sub-library: termcore_workspace_detectors (zero internal dependencies)
# ===========================================================================
set(TERMCORE_WORKSPACE_DETECTORS_SOURCES
    src/git_branch_detector.cpp
    src/port_detector.cpp
    src/pr_detector.cpp
)

add_library(termcore_workspace_detectors STATIC ${TERMCORE_WORKSPACE_DETECTORS_SOURCES})

target_include_directories(termcore_workspace_detectors
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_sanitizers(termcore_workspace_detectors)

# ===========================================================================
# Sub-library: termcore_config_core (depends on config.h/profile.h headers)
# ===========================================================================
set(TERMCORE_CONFIG_CORE_SOURCES
    src/config_diff.cpp
    src/config_field_registry.cpp
    src/config_themes.cpp
    src/config_value_adapter.cpp
    src/settings_model.cpp
    src/ghostty_config_import.cpp
)

add_library(termcore_config_core STATIC ${TERMCORE_CONFIG_CORE_SOURCES})

target_include_directories(termcore_config_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(termcore_config_core PUBLIC nlohmann_json::nlohmann_json)

enable_sanitizers(termcore_config_core)

# ===========================================================================
# Sub-library: termcore_session (depends on mux.h, result.h, base64.h headers)
# ===========================================================================
set(TERMCORE_SESSION_SOURCES
    src/session_paths.cpp
    src/session_serializer.cpp
    src/session_deserializer.cpp
    src/pane_status.cpp
    src/session_manager.cpp
    src/session_autosave.cpp
)

add_library(termcore_session STATIC ${TERMCORE_SESSION_SOURCES})

target_include_directories(termcore_session
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(termcore_session PUBLIC nlohmann_json::nlohmann_json ZLIB::ZLIB)

enable_sanitizers(termcore_session)

# ===========================================================================
# Main library: termcore (reduced source list)
# ===========================================================================
set(TERMCORE_SOURCES
    src/termcore_api.cpp
    src/vt_parser.cpp
    src/vt_parser_simd.cpp
    src/dynamic_colors.cpp
    src/contrast.cpp
    src/compressed_row.cpp
    src/scrollback_ring.cpp
    src/screen.cpp
    src/screen_csi.cpp
    src/screen_csi_ext.cpp
    src/screen_osc.cpp
    src/mouse.cpp
    src/mux.cpp
    src/mux_layout.cpp
    src/notification.cpp
    src/notification_visual.cpp
    src/config.cpp
    src/agent.cpp
    src/agent_orchestrator.cpp
    src/workspace_status.cpp
    src/search.cpp
    src/url_detector.cpp
    src/url_highlight.cpp
    src/kitty_keyboard.cpp
    src/base64.cpp
    src/shell_integration.cpp
    src/paste_guard.cpp
    src/vi_copy_mode.cpp
    src/webview_stub.cpp
    src/terminfo.cpp
    src/socket/command_dispatcher_mux.cpp
    src/socket/command_dispatcher_misc.cpp
    src/socket/command_dispatcher_browser.cpp
    src/socket/command_dispatcher_agent.cpp
    src/socket/command_dispatcher_orchestrator.cpp
    src/socket/command_dispatcher_terminal.cpp
    src/socket/command_dispatcher_status.cpp
    src/socket/socket_server.cpp
    src/screen_capture.cpp
    src/screen_export.cpp
    src/mcp_server_spec.cpp
    src/quick_terminal.cpp
    src/clipboard_history.cpp
    src/terminal_inspector.cpp
    src/terminal_controller.cpp
    src/input_handler.cpp
    src/selection_manager.cpp
    src/search_controller.cpp
    src/search_history.cpp
    src/tab_controller.cpp
    src/config_applier.cpp
    src/profile_manager.cpp
    src/profile_dropdown.cpp
    src/shell_detector.cpp
    src/update_checker.cpp
    src/command_palette.cpp
    src/grapheme.cpp
    src/instant_replay.cpp
    src/annotations.cpp
    src/serial_port.cpp
    src/password_manager.cpp
    src/tmux_integration.cpp
    src/history_provider.cpp
    src/completion_manager.cpp
    src/agent_tree_tracker.cpp
    src/hook_bridge.cpp
    src/hooks_installer.cpp
    src/provider_registry.cpp
    src/sidebar_model.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOURCES
        src/pty_unix.cpp
        src/shell_detector_unix.cpp
    )
endif()

if(WIN32)
    list(APPEND TERMCORE_SOURCES
        src/pty_windows.cpp
        src/shell_detector_windows.cpp
    )
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
            # Preserve LUA_LIBRARIES if already set (e.g. by vcpkg wrapper);
            # only override when LUA_LIBRARY is explicitly provided.
            if(LUA_LIBRARY)
                set(LUA_LIBRARIES ${LUA_LIBRARY})
            endif()
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
    # Lua binding modules (auto-discovered)
    file(GLOB LUA_BINDING_SOURCES src/lua_bindings/*.cpp)
    list(APPEND TERMCORE_SOURCES ${LUA_BINDING_SOURCES})
    message(STATUS "Lua plugin system: ENABLED (${LUA_VERSION})")
else()
    message(FATAL_ERROR "Lua 5.4 is required but not found. Install lua5.4 and liblua5.4-dev.")
endif()

# lua_config.cpp has both TERMCORE_HAS_LUA and stub paths, always compile it
# lua_config_writer.cpp has no Lua runtime dependency — always compile it
# plugin_manager.cpp has both TERMCORE_HAS_LUA and stub paths, always compile it
list(APPEND TERMCORE_SOURCES src/lua_config.cpp src/lua_config_writer.cpp src/plugin_manager.cpp)

add_library(termcore STATIC ${TERMCORE_SOURCES})

# SIMD: compile vt_parser_simd.cpp with AVX2 so both AVX2 and SSE2 codepaths
# are available; runtime CPU dispatch selects the best one.
if(MSVC)
    set_source_files_properties(src/vt_parser_simd.cpp
        PROPERTIES COMPILE_FLAGS "/arch:AVX2")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/vt_parser_simd.cpp
        PROPERTIES COMPILE_FLAGS "-mavx2")
endif()

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${HARFBUZZ_INCLUDE_DIRS}
)

target_link_libraries(termcore PUBLIC
    termcore_font termcore_ssh termcore_image
    termcore_shader termcore_theme termcore_socket_transport
    termcore_keybinding termcore_workspace_detectors
    termcore_config_core termcore_session
    ICU::uc nlohmann_json::nlohmann_json ZLIB::ZLIB
)
target_link_directories(termcore PUBLIC ${HARFBUZZ_LIBRARY_DIRS})

# Embed defaults/*.lua as C byte arrays
if(TERMCORE_HAS_LUA)
    set(DEFAULT_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defaults)
    set(LUA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/lua_generated)
    file(MAKE_DIRECTORY ${LUA_GENERATED_DIR})

    set(DEFAULT_LUA_NAMES
        config colors keybindings
        tab_title paste_guard themes
        providers
    )

    set(LUA_GENERATED_HEADERS "")
    foreach(NAME ${DEFAULT_LUA_NAMES})
        set(LUA_INPUT ${DEFAULT_LUA_DIR}/${NAME}.lua)
        set(LUA_OUTPUT ${LUA_GENERATED_DIR}/default_${NAME}_lua.h)
        add_custom_command(
            OUTPUT ${LUA_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_lua.cmake
                ${LUA_INPUT} ${LUA_OUTPUT} default_${NAME}_lua
            DEPENDS ${LUA_INPUT}
            COMMENT "Embedding defaults/${NAME}.lua"
        )
        list(APPEND LUA_GENERATED_HEADERS ${LUA_OUTPUT})
    endforeach()

    add_custom_target(generate_lua_defaults DEPENDS ${LUA_GENERATED_HEADERS})
    add_dependencies(termcore generate_lua_defaults)
    target_include_directories(termcore PRIVATE ${LUA_GENERATED_DIR})
endif()

if(TERMCORE_HAS_LUA)
    target_include_directories(termcore PUBLIC ${LUA_INCLUDE_DIRS})
    target_include_directories(termcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(termcore PUBLIC sol2_header_only ${LUA_LIBRARIES})
    target_link_directories(termcore PUBLIC ${LUA_LIBRARY_DIRS})
    target_compile_definitions(termcore PUBLIC TERMCORE_HAS_LUA=1)
endif()

# Apply sanitizers and strict warnings to all targets
enable_sanitizers(termcore)
