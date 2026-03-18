set(TERMCORE_SOURCES
    src/termcore_api.cpp
    src/vt_parser.cpp
    src/screen.cpp
    src/screen_csi.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOURCES src/pty_unix.cpp)
endif()

add_library(termcore STATIC ${TERMCORE_SOURCES})

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
