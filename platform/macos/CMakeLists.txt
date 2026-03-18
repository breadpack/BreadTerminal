if(NOT APPLE)
    return()
endif()

add_library(termcore_macos STATIC
    src/CoreTextRasterizer.mm
    src/CoreTextDiscovery.mm
    src/MetalAtlasUploader.mm
    src/MetalTextRenderer.mm
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
        "-framework Foundation"
        "-framework Metal"
        "-framework QuartzCore"
)

# Enable Objective-C++ for .mm files
set_source_files_properties(
    src/CoreTextRasterizer.mm
    src/CoreTextDiscovery.mm
    src/MetalAtlasUploader.mm
    src/MetalTextRenderer.mm
    PROPERTIES COMPILE_FLAGS "-x objective-c++"
)

# --- Metal shader compilation (optional: requires Metal Toolchain) ---
# If xcrun metal is available, pre-compile shaders into default.metallib.
# Otherwise, the renderer will compile shaders from source at runtime.
execute_process(
    COMMAND xcrun metal -v
    RESULT_VARIABLE METAL_COMPILER_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
)

if(METAL_COMPILER_RESULT EQUAL 0)
    set(METAL_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/src/Shaders/cell.metal)
    set(METAL_AIR ${CMAKE_CURRENT_BINARY_DIR}/cell.air)
    set(METAL_LIB ${CMAKE_CURRENT_BINARY_DIR}/default.metallib)

    add_custom_command(
        OUTPUT ${METAL_AIR}
        COMMAND xcrun metal -c ${METAL_SHADER} -o ${METAL_AIR}
        DEPENDS ${METAL_SHADER}
        COMMENT "Compiling Metal shader cell.metal -> cell.air"
    )

    add_custom_command(
        OUTPUT ${METAL_LIB}
        COMMAND xcrun metallib ${METAL_AIR} -o ${METAL_LIB}
        DEPENDS ${METAL_AIR}
        COMMENT "Linking Metal library -> default.metallib"
    )

    add_custom_target(metal_shaders ALL DEPENDS ${METAL_LIB})
    add_dependencies(termcore_macos metal_shaders)
else()
    message(STATUS "Metal compiler not found; shaders will be compiled at runtime")
endif()

# ---------------------------------------------------------------------------
# Application executable
# ---------------------------------------------------------------------------
add_executable(BreadTerminal MACOSX_BUNDLE
    src/main.mm
    src/AppDelegate.mm
    src/TerminalView.mm
)

target_include_directories(BreadTerminal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(BreadTerminal PRIVATE
    termcore_macos
)

target_link_libraries(BreadTerminal PRIVATE
    "-framework Cocoa"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework CoreText"
    "-framework CoreGraphics"
)

# Enable Objective-C++ with ARC for app source files
set_source_files_properties(
    src/main.mm
    src/AppDelegate.mm
    src/TerminalView.mm
    PROPERTIES COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
)

# Set bundle properties
set_target_properties(BreadTerminal PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.breadterminal.app"
    MACOSX_BUNDLE_BUNDLE_NAME "BreadTerminal"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
)

# Copy default.metallib into the bundle Resources if available
if(METAL_COMPILER_RESULT EQUAL 0)
    add_dependencies(BreadTerminal metal_shaders)
    add_custom_command(TARGET BreadTerminal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:BreadTerminal>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${METAL_LIB}
            "$<TARGET_BUNDLE_DIR:BreadTerminal>/Contents/Resources/default.metallib"
        COMMENT "Copying default.metallib into BreadTerminal.app bundle"
    )
endif()
