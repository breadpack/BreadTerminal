cmake_minimum_required(VERSION 3.24)
project(BreadTerminal
    VERSION 0.1.0
    LANGUAGES CXX OBJCXX
    DESCRIPTION "Cross-platform AI coding agent terminal"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(core)

if(APPLE)
    add_subdirectory(platform/macos)
endif()

if(UNIX AND NOT APPLE)
    add_subdirectory(platform/linux)
endif()

add_subdirectory(tests)
