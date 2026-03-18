set(TERMCORE_SOURCES
    src/termcore_api.cpp
    src/vt_parser.cpp
    src/screen.cpp
    src/screen_csi.cpp
    src/font/unicode_width.cpp
)

if(UNIX)
    list(APPEND TERMCORE_SOURCES src/pty_unix.cpp)
endif()

# ICU for Unicode properties
set(ICU_ROOT "/opt/homebrew/opt/icu4c@77")
list(APPEND CMAKE_PREFIX_PATH "${ICU_ROOT}")
find_package(ICU REQUIRED COMPONENTS uc)

add_library(termcore STATIC ${TERMCORE_SOURCES})

target_include_directories(termcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(termcore PUBLIC ICU::uc)
