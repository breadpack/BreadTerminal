add_executable(bread_benchmarks
    run_benchmarks.cpp
    bench_vt_parser.cpp
    bench_screen.cpp
    bench_font.cpp
    bench_cell_builder.cpp
    bench_e2e.cpp
    bench_image_protocols.cpp
    bench_scrollback.cpp
    bench_threading.cpp
    bench_agent.cpp
    bench_lua.cpp
)

target_include_directories(bread_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core/src
)

target_link_libraries(bread_benchmarks PRIVATE termcore)

enable_sanitizers(bread_benchmarks)

# Optimize for benchmarking even in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Benchmarks: Consider building with -DCMAKE_BUILD_TYPE=Release for accurate results")
endif()
