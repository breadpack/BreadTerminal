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
    src/TerminalWidget.cpp
    src/TerminalInput.cpp
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

# Application
add_executable(breadterminal
    src/main.cpp
)

target_link_libraries(breadterminal PRIVATE termcore_linux)
