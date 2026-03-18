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
