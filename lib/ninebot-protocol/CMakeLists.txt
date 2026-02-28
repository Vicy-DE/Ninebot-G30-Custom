##
## Ninebot G30 Max Protocol Library - CMake Build
##
## This is a header-only library reconstructed from reverse-engineered firmware.
## The test suite validates protocol parsing, UART state machine, register I/O,
## and full three-board simulation.
##
## Build:
##   mkdir build && cd build && cmake .. && cmake --build .
##
## Or with Makefile directly:
##   g++ -std=c++17 -I include -O2 -Wall -Wextra -o test_main test/test_main.cpp
##

cmake_minimum_required(VERSION 3.10)
project(ninebot_protocol
    VERSION 1.0.0
    DESCRIPTION "Ninebot G30 Max Protocol Library (reverse-engineered)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Header-only library — INTERFACE target
add_library(ninebot_protocol INTERFACE)
target_include_directories(ninebot_protocol INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

## Test executable
add_executable(test_main test/test_main.cpp)
target_link_libraries(test_main PRIVATE ninebot_protocol)
target_compile_options(test_main PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

## CTest integration
enable_testing()
add_test(NAME protocol_tests COMMAND test_main)
