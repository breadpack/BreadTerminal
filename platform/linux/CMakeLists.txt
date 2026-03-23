if(NOT UNIX OR APPLE)
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(EPOXY REQUIRED epoxy)  # For OpenGL

# FreeType rasterizer
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

# --- WebKitGTK (optional) ---
set(HAS_WEBKIT OFF)
pkg_check_modules(WEBKIT webkit2gtk-4.1)
if(NOT WEBKIT_FOUND)
    pkg_check_modules(WEBKIT webkit2gtk-4.0)
endif()
if(WEBKIT_FOUND)
    set(HAS_WEBKIT ON)
    message(STATUS "WebKitGTK: FOUND (${WEBKIT_VERSION})")
else()
    message(STATUS "WebKitGTK: NOT FOUND (WebView will be stub)")
endif()

add_library(termcore_linux STATIC
    src/FreeTypeRasterizer.cpp
    src/FontconfigDiscovery.cpp
    src/GLAtlasUploader.cpp
    src/GLTextRenderer.cpp
    src/GLCellBuilder.cpp
    src/GLCellBuilderOverlays.cpp
    src/GLCellBuilderPaneStatus.cpp
    src/GLCellBuilderCommandPalette.cpp
    src/GtkPlatformHost.cpp
    src/TerminalWidget.cpp
    src/TerminalInput.cpp
    src/UnifiedSettingsWindow.cpp
    src/UnifiedSettingsSidebar.cpp
    src/UnifiedSettingsContent.cpp
    src/UnifiedSettingsThemeCards.cpp
    src/UnifiedSettingsFontCards.cpp
    src/UnifiedSettingsKeybindings.cpp
    src/UnifiedSettingsKeyCaptureDialog.cpp
    src/FontInstaller.cpp
    src/WebKitWebViewBridge.cpp
)

target_include_directories(termcore_linux
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GTK4_INCLUDE_DIRS}
        ${EPOXY_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
        ${FONTCONFIG_INCLUDE_DIRS}
)

# TERMCORE_HAS_WEBVIEW suppresses the core stub (webview_stub.cpp) so
# createWebView() is provided by WebKitWebViewBridge.cpp on Linux.
target_compile_definitions(termcore_linux PUBLIC TERMCORE_HAS_WEBVIEW=1)

if(HAS_WEBKIT)
    target_include_directories(termcore_linux PUBLIC ${WEBKIT_INCLUDE_DIRS})
    target_compile_definitions(termcore_linux PUBLIC TERMCORE_HAS_WEBKIT=1)
endif()

target_link_libraries(termcore_linux
    PUBLIC
        termcore
        ${GTK4_LIBRARIES}
        ${EPOXY_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${FONTCONFIG_LIBRARIES}
)

if(HAS_WEBKIT)
    target_link_libraries(termcore_linux PUBLIC ${WEBKIT_LIBRARIES})
endif()

# ─── Optional Wayland native backend ─────────────────────────────
pkg_check_modules(WAYLAND_CLIENT wayland-client)
pkg_check_modules(WAYLAND_EGL wayland-egl)
pkg_check_modules(EGL egl)
pkg_check_modules(XKBCOMMON xkbcommon)

if(WAYLAND_CLIENT_FOUND AND WAYLAND_EGL_FOUND AND EGL_FOUND AND XKBCOMMON_FOUND)
    message(STATUS "Wayland support: enabled")

    add_library(termcore_wayland STATIC
        src/WaylandWindow.cpp
        src/WaylandClipboard.cpp
        src/WaylandPlatformHost.cpp
    )

    target_include_directories(termcore_wayland
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${WAYLAND_CLIENT_INCLUDE_DIRS}
            ${WAYLAND_EGL_INCLUDE_DIRS}
            ${EGL_INCLUDE_DIRS}
            ${XKBCOMMON_INCLUDE_DIRS}
            ${FREETYPE_INCLUDE_DIRS}
            ${FONTCONFIG_INCLUDE_DIRS}
    )

    target_link_libraries(termcore_wayland
        PUBLIC
            termcore
            ${WAYLAND_CLIENT_LIBRARIES}
            ${WAYLAND_EGL_LIBRARIES}
            ${EGL_LIBRARIES}
            ${XKBCOMMON_LIBRARIES}
            ${FREETYPE_LIBRARIES}
            ${FONTCONFIG_LIBRARIES}
    )

    target_compile_definitions(termcore_wayland PUBLIC TERMCORE_HAS_WAYLAND=1)

    # Wayland protocol headers (xdg-shell) are typically provided by
    # wayland-protocols; if not pre-generated, use wayland-scanner.

    # ─── Generate zwp_text_input_v3 protocol glue ────────────────
    pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
    find_program(WAYLAND_SCANNER wayland-scanner)

    if(WAYLAND_PROTOCOLS_FOUND AND WAYLAND_SCANNER)
        # Locate the protocol XML from wayland-protocols package
        execute_process(
            COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
            OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(TEXT_INPUT_V3_XML
            "${WAYLAND_PROTOCOLS_DIR}/unstable/text-input/text-input-unstable-v3.xml")

        if(EXISTS "${TEXT_INPUT_V3_XML}")
            set(TEXT_INPUT_V3_HEADER
                "${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-client-protocol.h")
            set(TEXT_INPUT_V3_CODE
                "${CMAKE_CURRENT_BINARY_DIR}/text-input-unstable-v3-protocol.c")

            add_custom_command(
                OUTPUT "${TEXT_INPUT_V3_HEADER}"
                COMMAND ${WAYLAND_SCANNER} client-header
                        "${TEXT_INPUT_V3_XML}" "${TEXT_INPUT_V3_HEADER}"
                DEPENDS "${TEXT_INPUT_V3_XML}"
                COMMENT "Generating text-input-unstable-v3 client header"
            )
            add_custom_command(
                OUTPUT "${TEXT_INPUT_V3_CODE}"
                COMMAND ${WAYLAND_SCANNER} private-code
                        "${TEXT_INPUT_V3_XML}" "${TEXT_INPUT_V3_CODE}"
                DEPENDS "${TEXT_INPUT_V3_XML}"
                COMMENT "Generating text-input-unstable-v3 protocol code"
            )

            target_sources(termcore_wayland PRIVATE
                "${TEXT_INPUT_V3_HEADER}"
                "${TEXT_INPUT_V3_CODE}"
            )
            target_include_directories(termcore_wayland
                PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
            )
            target_compile_definitions(termcore_wayland
                PRIVATE TERMCORE_HAS_TEXT_INPUT_V3=1
            )
            message(STATUS "Wayland text-input-v3 IME support: enabled")
        else()
            message(STATUS "Wayland text-input-v3 IME support: disabled "
                           "(protocol XML not found)")
        endif()
    else()
        message(STATUS "Wayland text-input-v3 IME support: disabled "
                       "(wayland-scanner or wayland-protocols not found)")
    endif()
else()
    message(STATUS "Wayland support: disabled (missing dependencies)")
endif()

# Application
add_executable(breadterminal
    src/main.cpp
)

target_link_libraries(breadterminal PRIVATE termcore_linux)
if(TARGET termcore_wayland)
    target_link_libraries(breadterminal PRIVATE termcore_wayland)
    target_compile_definitions(breadterminal PRIVATE TERMCORE_HAS_WAYLAND=1)
endif()

# Install icon files for hicolor icon theme
set(ICON_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
foreach(SIZE 16 24 32 48 64 128 256 512)
    install(FILES "${ICON_RESOURCE_DIR}/breadterminal_${SIZE}.png"
        DESTINATION "share/icons/hicolor/${SIZE}x${SIZE}/apps"
        RENAME "breadterminal.png"
    )
endforeach()

# Install .desktop file
install(FILES "${ICON_RESOURCE_DIR}/breadterminal.desktop"
    DESTINATION "share/applications"
)
