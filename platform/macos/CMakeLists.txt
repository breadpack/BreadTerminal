if(NOT APPLE)
    return()
endif()

add_library(termcore_macos STATIC
    src/CoreTextRasterizer.mm
)

target_include_directories(termcore_macos
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(termcore_macos
    PUBLIC
        termcore
)

# Link macOS frameworks
target_link_libraries(termcore_macos
    PUBLIC
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
)

# Enable Objective-C++ for .mm files
set_source_files_properties(src/CoreTextRasterizer.mm
    PROPERTIES COMPILE_FLAGS "-x objective-c++"
)
