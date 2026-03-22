add_executable(bread_benchmarks
    run_benchmarks.cpp
    bench_vt_parser.cpp
    bench_screen.cpp
    bench_font.cpp
    bench_cell_builder.cpp
    bench_e2e.cpp
)

target_include_directories(bread_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(bread_benchmarks PRIVATE termcore)

# Optimize for benchmarking even in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Benchmarks: Consider building with -DCMAKE_BUILD_TYPE=Release for accurate results")
endif()
