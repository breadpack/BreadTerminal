if(NOT WIN32)
    return()
endif()

add_library(termcore_windows STATIC
    src/DirectWriteRasterizer.cpp
    src/DirectWriteDiscovery.cpp
    src/D3DAtlasUploader.cpp
    src/D3DTextRenderer.cpp
)

target_include_directories(termcore_windows PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(termcore_windows PUBLIC
    termcore dwrite d2d1 d3d11 dxgi windowsapp
)

add_executable(BreadTerminal WIN32
    src/main.cpp
    src/TerminalWindow.cpp
)
target_link_libraries(BreadTerminal PRIVATE termcore_windows)
