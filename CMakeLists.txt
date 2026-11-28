cmake_minimum_required(VERSION 3.20)
project(vaultsync)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional AddressSanitizer for Debug builds (the "asan" preset). Off by
# default: ASan replaces the CRT debug heap, which disables the leak check in
# tests, and MSVC ASan has no leak detection of its own.
# MSVC refuses /RTC1 together with /fsanitize=address, so drop it when ASan is on.
option(VS_ASAN "Build Debug with AddressSanitizer (MSVC)" OFF)
if(MSVC AND VS_ASAN)
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blake3/c/blake3.h)
    message(FATAL_ERROR "BLAKE3 missing. Run: git submodule update --init")
endif()

enable_testing()

#BLAKE3
add_library(blake3 STATIC
    third_party/blake3/c/blake3.c
    third_party/blake3/c/blake3_dispatch.c
    third_party/blake3/c/blake3_portable.c
)
target_include_directories(blake3 PUBLIC third_party/blake3/c)
target_compile_definitions(blake3 PRIVATE
    BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512
)

# core library
add_library(vaultsync_core STATIC 
    core/vaultsync.c
    core/util.c
    core/scanner.c
    core/hash.c
    core/storage_local.c
    core/manifest.c
)
target_include_directories(vaultsync_core PUBLIC 
    core/include
)
target_link_libraries(vaultsync_core PRIVATE
    blake3
)

# tests
add_executable(vaultsync_tests
    tests/test_main.c
)
target_link_libraries(vaultsync_tests PRIVATE
    vaultsync_core
)
target_include_directories(vaultsync_tests PRIVATE
    core
)
target_compile_definitions(vaultsync_tests PRIVATE
    VS_TEST_DIR="${CMAKE_BINARY_DIR}/tests"
)
# Register every TEST(...) entry from test_main.c as its own ctest test, so
# ctest reports pass/fail per test and `ctest -R <name>` runs just one.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests/test_main.c)
file(STRINGS tests/test_main.c VS_TEST_LINES REGEX "^[ \t]*TEST\\(test_[A-Za-z0-9_]+\\)")
foreach(line ${VS_TEST_LINES})
    string(REGEX MATCH "test_[A-Za-z0-9_]+" name "${line}")
    add_test(NAME ${name} COMMAND vaultsync_tests ${name})
endforeach()

# CLI
add_executable(vaultsync_cli 
    cli/main.c
)
target_link_libraries(vaultsync_cli PRIVATE 
    vaultsync_core
)
target_include_directories(vaultsync_cli PRIVATE
    core
)

foreach(t vaultsync_core vaultsync_cli vaultsync_tests)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
        if(VS_ASAN)
            target_compile_options(${t} PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
        endif()
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

# The ASan runtime DLL lives next to cl.exe, which is only on PATH inside the
# VS developer shell. Copy it next to the executables so they run from anywhere.
if(MSVC AND VS_ASAN)
    get_filename_component(VS_MSVC_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    foreach(t vaultsync_cli vaultsync_tests)
        target_link_options(${t} PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL:NO>)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${VS_MSVC_BIN_DIR}/clang_rt.asan_dynamic-x86_64.dll"
                    $<TARGET_FILE_DIR:${t}>
        )
    endforeach()
endif()