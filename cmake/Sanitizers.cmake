# Sanitizers.cmake - Address, Thread, and Undefined Behavior sanitizer support
#
# Usage:
#   include(cmake/Sanitizers.cmake)
#   enable_sanitizers(my_target)
#
# Options (set before including or via -D on command line):
#   ENABLE_ASAN  - Address Sanitizer (detects memory errors)
#   ENABLE_TSAN  - Thread Sanitizer (detects data races)
#   ENABLE_UBSAN - Undefined Behavior Sanitizer

option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" OFF)

# Validate: ASan and TSan are mutually exclusive
if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR
        "ASan and TSan are mutually exclusive. "
        "Please enable only one of ENABLE_ASAN or ENABLE_TSAN.")
endif()

# MSVC does not support TSan
if(ENABLE_TSAN AND MSVC)
    message(WARNING
        "Thread Sanitizer (TSan) is not supported by MSVC. "
        "ENABLE_TSAN will be ignored on this compiler. "
        "Use Clang or GCC on Linux/macOS for TSan support.")
    set(ENABLE_TSAN OFF)
endif()

# Print status
if(ENABLE_ASAN)
    message(STATUS "Sanitizer: Address Sanitizer (ASan) ENABLED")
endif()
if(ENABLE_TSAN)
    message(STATUS "Sanitizer: Thread Sanitizer (TSan) ENABLED")
endif()
if(ENABLE_UBSAN)
    message(STATUS "Sanitizer: Undefined Behavior Sanitizer (UBSan) ENABLED")
endif()
if(ENABLE_STRICT_WARNINGS)
    message(STATUS "Strict compiler warnings ENABLED")
endif()

# enable_sanitizers(target)
# Applies sanitizer compile and link flags to the given target.
function(enable_sanitizers target)
    # -- Sanitizer flags --
    if(MSVC)
        # MSVC: Only ASan is supported (since VS2019 16.9)
        if(ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address)
            # MSVC ASan does not require special link flags; the runtime is
            # linked automatically. However, we must disable incremental
            # linking which is incompatible with ASan.
            target_link_options(${target} PRIVATE /INCREMENTAL:NO)
        endif()

        # MSVC does not support TSan (handled above with warning + OFF)

        # MSVC does not have a standalone UBSan flag; /fsanitize=address
        # already includes some UB checks. Print a note if requested.
        if(ENABLE_UBSAN AND NOT ENABLE_ASAN)
            message(WARNING
                "MSVC does not support standalone UBSan. "
                "Consider using ENABLE_ASAN which includes some UB detection, "
                "or use Clang/GCC for full UBSan support.")
        endif()
    else()
        # GCC / Clang
        set(_sanitizer_compile_flags "")
        set(_sanitizer_link_flags "")

        if(ENABLE_ASAN)
            list(APPEND _sanitizer_compile_flags -fsanitize=address -fno-omit-frame-pointer)
            list(APPEND _sanitizer_link_flags -fsanitize=address)
        endif()

        if(ENABLE_TSAN)
            list(APPEND _sanitizer_compile_flags -fsanitize=thread)
            list(APPEND _sanitizer_link_flags -fsanitize=thread)
        endif()

        if(ENABLE_UBSAN)
            list(APPEND _sanitizer_compile_flags -fsanitize=undefined -fno-omit-frame-pointer)
            list(APPEND _sanitizer_link_flags -fsanitize=undefined)
        endif()

        if(_sanitizer_compile_flags)
            target_compile_options(${target} PRIVATE ${_sanitizer_compile_flags})
            target_link_options(${target} PRIVATE ${_sanitizer_link_flags})
        endif()
    endif()

    # -- Strict warnings --
    if(ENABLE_STRICT_WARNINGS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endfunction()
