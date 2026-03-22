if(NOT UNIX OR APPLE)
    return()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(EPOXY REQUIRED epoxy)  # For OpenGL

# FreeType rasterizer
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

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
)

target_include_directories(termcore_linux
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GTK4_INCLUDE_DIRS}
        ${EPOXY_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
        ${FONTCONFIG_INCLUDE_DIRS}
)

target_link_libraries(termcore_linux
    PUBLIC
        termcore
        ${GTK4_LIBRARIES}
        ${EPOXY_LIBRARIES}
        ${FREETYPE_LIBRARIES}
        ${FONTCONFIG_LIBRARIES}
)

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
    # For now we assume xdg-shell-client-protocol.h is available in
    # the system include path (standard on most distros).
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
