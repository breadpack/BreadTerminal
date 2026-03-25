if(NOT WIN32)
    return()
endif()

# --- Sentry Native SDK (crash reporting) ---
include(FetchContent)
FetchContent_Declare(sentry_native
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG 0.9.1
)
set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SENTRY_BACKEND "crashpad" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(sentry_native)

# --- WebView2 SDK (optional) ---
set(HAS_WEBVIEW2 OFF)
find_path(WEBVIEW2_INCLUDE_DIR WebView2.h
    HINTS
        "$ENV{NUGET_PACKAGES}/microsoft.web.webview2/1.0.2903.40/build/native/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/include"
)
if(WEBVIEW2_INCLUDE_DIR)
    set(HAS_WEBVIEW2 ON)
    add_library(webview2_sdk INTERFACE)
    target_include_directories(webview2_sdk INTERFACE "${WEBVIEW2_INCLUDE_DIR}")
    message(STATUS "WebView2 SDK: FOUND at ${WEBVIEW2_INCLUDE_DIR}")
else()
    message(STATUS "WebView2 SDK: NOT FOUND (WebView2Host will be stub)")
endif()

# --- termcore_windows static library ---
set(TERMCORE_WIN_SOURCES
    src/DirectWriteRasterizer.cpp
    src/DirectWriteDiscovery.cpp
    src/D3DAtlasUploader.cpp
    src/D3DTextRenderer.cpp
    src/D3DCellBuilder.cpp
    src/D3DCellBuilderOverlays.cpp
    src/D3DCellBuilderPaneStatus.cpp
    src/D3DCellBuilderCommandPalette.cpp
    src/D3DCellBuilderProfileDropdown.cpp
    src/D3DCellBuilderSidebar.cpp
    src/ConfigWatcherWin.cpp
    src/TerminalWindowIME.cpp
    src/TerminalWindowNotify.cpp
    src/D3DImageRenderer.cpp
    src/D3DBackgroundImage.cpp
    src/D3DCustomShader.cpp
    src/FontInstaller.cpp
    src/UnifiedSettingsWindow.cpp
    src/UnifiedSettingsLayout.cpp
    src/UnifiedSettingsSidebar.cpp
    src/UnifiedSettingsContent.cpp
    src/UnifiedSettingsThemeCards.cpp
    src/UnifiedSettingsFontCards.cpp
    src/UnifiedSettingsKeybindings.cpp
    src/UnifiedSettingsSearch.cpp
    src/TerminalAccessibility.cpp
    src/TerminalTextRange.cpp
    src/HighContrastDetector.cpp
    src/ClipboardHistoryPopup.cpp
)

# WebView2Bridge.cpp has internal #if guards; always compile it.
# It provides createWebView() for both HAS_WEBVIEW2 and stub paths.
list(APPEND TERMCORE_WIN_SOURCES src/WebView2Bridge.cpp)

add_library(termcore_windows STATIC ${TERMCORE_WIN_SOURCES})

target_include_directories(termcore_windows PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(termcore_windows PUBLIC
    termcore dwrite d2d1 d3d11 dxgi d3dcompiler windowsapp imm32 dwmapi shlwapi
)

# TERMCORE_HAS_WEBVIEW suppresses the core stub (webview_stub.cpp) so
# createWebView() is provided by WebView2Bridge.cpp on Windows.
target_compile_definitions(termcore_windows PUBLIC TERMCORE_HAS_WEBVIEW=1)

if(HAS_WEBVIEW2)
    target_link_libraries(termcore_windows PUBLIC webview2_sdk)
    target_compile_definitions(termcore_windows PUBLIC HAS_WEBVIEW2=1)
endif()

# --- BreadTerminal executable ---
add_executable(BreadTerminal WIN32
    src/main.cpp
    src/TerminalWindow.cpp
    src/TerminalWindowState.cpp
    src/TerminalWindowStateEvents.cpp
    src/TerminalWindowInput.cpp
    src/QuickTerminalWindow.cpp
    resources/BreadTerminal.rc
)
target_link_libraries(BreadTerminal PRIVATE termcore_windows comctl32 shell32 gdiplus sentry::sentry)

# Copy crashpad_handler.exe next to BreadTerminal.exe (required by Sentry crashpad backend)
add_custom_command(TARGET BreadTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:crashpad_handler>"
        "$<TARGET_FILE_DIR:BreadTerminal>/crashpad_handler.exe"
)

# Copy theme_index.json to output directory
add_custom_command(TARGET BreadTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/theme_index.json"
        "$<TARGET_FILE_DIR:BreadTerminal>/theme_index.json"
)
add_custom_command(TARGET BreadTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/font_index.json"
        "$<TARGET_FILE_DIR:BreadTerminal>/font_index.json"
)
# Copy icon to output directory (for installer packaging)
add_custom_command(TARGET BreadTerminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/BreadTerminal.ico"
        "$<TARGET_FILE_DIR:BreadTerminal>/BreadTerminal.ico"
)
