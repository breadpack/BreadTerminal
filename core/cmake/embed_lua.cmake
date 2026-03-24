# core/cmake/embed_lua.cmake
# Usage: cmake -P embed_lua.cmake <input.lua> <output.h> <variable_name>
#
# Converts a .lua file into a C header with:
#   const unsigned char <variable_name>[] = { 0x2d, 0x2d, ... };
#   const unsigned int <variable_name>_len = <size>;

set(INPUT_FILE "${CMAKE_ARGV3}")
set(OUTPUT_FILE "${CMAKE_ARGV4}")
set(VAR_NAME "${CMAKE_ARGV5}")

file(READ "${INPUT_FILE}" CONTENT HEX)
string(LENGTH "${CONTENT}" HEX_LEN)
math(EXPR BYTE_COUNT "${HEX_LEN} / 2")

# Convert hex pairs to 0xNN format
set(HEX_ARRAY "")
set(LINE_COUNT 0)
string(REGEX MATCHALL ".." HEX_PAIRS "${CONTENT}")
foreach(PAIR ${HEX_PAIRS})
    if(HEX_ARRAY)
        string(APPEND HEX_ARRAY ",")
        if(LINE_COUNT GREATER_EQUAL 16)
            string(APPEND HEX_ARRAY "\n    ")
            set(LINE_COUNT 0)
        endif()
    endif()
    string(APPEND HEX_ARRAY "0x${PAIR}")
    math(EXPR LINE_COUNT "${LINE_COUNT} + 1")
endforeach()

file(WRITE "${OUTPUT_FILE}"
    "#pragma once\n"
    "// Auto-generated from ${INPUT_FILE} — do not edit\n"
    "static const unsigned char ${VAR_NAME}[] = {\n    ${HEX_ARRAY}\n};\n"
    "static const unsigned int ${VAR_NAME}_len = ${BYTE_COUNT};\n"
)
