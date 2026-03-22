if(NOT WIN32)
    return()
endif()

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
    src/ConfigWatcherWin.cpp
    src/TerminalWindowIME.cpp
    src/TerminalWindowNotify.cpp
    src/D3DImageRenderer.cpp
    src/D3DBackgroundImage.cpp
    src/D3DCustomShader.cpp
    src/ThemeHubWindow.cpp
    src/ThemeHubPaint.cpp
    src/ThemeHubCards.cpp
    src/FontHubWindow.cpp
    src/FontHubPaint.cpp
    src/FontHubCards.cpp
    src/FontInstaller.cpp
    src/SettingsWindow.cpp
    src/SettingsPaint.cpp
    src/SettingsTabPanels.cpp
    src/SettingsControls.cpp
)

if(HAS_WEBVIEW2)
    list(APPEND TERMCORE_WIN_SOURCES src/WebView2Host.cpp)
endif()

add_library(termcore_windows STATIC ${TERMCORE_WIN_SOURCES})

target_include_directories(termcore_windows PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(termcore_windows PUBLIC
    termcore dwrite d2d1 d3d11 dxgi d3dcompiler windowsapp imm32 dwmapi shlwapi
)

if(HAS_WEBVIEW2)
    target_link_libraries(termcore_windows PUBLIC webview2_sdk)
    target_compile_definitions(termcore_windows PUBLIC HAS_WEBVIEW2=1)
endif()

# --- BreadTerminal executable ---
add_executable(BreadTerminal WIN32
    src/main.cpp
    src/TerminalWindow.cpp
    src/TerminalWindowState.cpp
    src/TerminalWindowInput.cpp
    src/QuickTerminalWindow.cpp
)
target_link_libraries(BreadTerminal PRIVATE termcore_windows comctl32 shell32 gdiplus)

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
