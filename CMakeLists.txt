cmake_minimum_required(VERSION 3.20)
project(vaultsync)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blake3/c/blake3.h)
    message(FATAL_ERROR "BLAKE3 missing. Run: git submodule update --init")
endif()

enable_testing()

# core library
add_library(vaultsync_core STATIC 
    core/vaultsync.c
    core/util.c
    core/scanner.c
)
target_include_directories(vaultsync_core PUBLIC 
    core/include
)

# tests
add_executable(vaultsync_tests tests/test_main.c)
target_link_libraries(vaultsync_tests PRIVATE vaultsync_core)
target_include_directories(vaultsync_tests PRIVATE core)
add_test(NAME unit COMMAND vaultsync_tests)

# CLI
add_executable(vaultsync_cli 
    cli/main.c
)
target_link_libraries(vaultsync_cli PRIVATE 
    vaultsync_core
)

foreach(t vaultsync_core vaultsync_cli vaultsync_tests)
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()