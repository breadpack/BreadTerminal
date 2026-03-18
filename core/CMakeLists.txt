add_library(termcore STATIC
    src/termcore_api.cpp
    src/vt_parser.cpp
    src/screen.cpp
    src/screen_csi.cpp
    src/pty_unix.cpp
)

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
