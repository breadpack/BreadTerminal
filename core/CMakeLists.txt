add_library(termcore STATIC
    src/termcore_api.cpp
)

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
