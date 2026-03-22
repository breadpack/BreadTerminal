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
    src/GtkPlatformHost.cpp
    src/TerminalWidget.cpp
    src/TerminalInput.cpp
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

# Application
add_executable(breadterminal
    src/main.cpp
)

target_link_libraries(breadterminal PRIVATE termcore_linux)
