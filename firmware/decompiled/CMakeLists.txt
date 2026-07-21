##
## Ninebot G30 Max — Decompiled Firmware Build System
##
## Builds three firmware simulation libraries (ESC, BLE, BMS) plus a
## comprehensive test suite that validates behavioral equivalence with
## the original stock firmware binaries.
##
## Build (host simulation):
##   mkdir build && cd build && cmake .. && cmake --build .
##   ctest --output-on-failure
##
## Cross-compile for STM32 (requires arm-none-eabi-gcc):
##   cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/stm32f103.cmake -DBUILD_FOR_TARGET=ON ..
##

cmake_minimum_required(VERSION 3.14)
project(ninebot_g30_firmware
    VERSION 1.0.0
    DESCRIPTION "Ninebot G30 Max decompiled firmware (ESC/BLE/BMS)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option: build for STM32 target vs. host simulation
option(BUILD_FOR_TARGET "Cross-compile for STM32F103 target" OFF)

# ─── Common HAL abstraction (header-only) ────────────────────────────
add_library(hal_common INTERFACE)
target_include_directories(hal_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

# ─── ESC firmware library ────────────────────────────────────────────
add_library(esc_firmware STATIC
    esc/src/esc_main.cpp
    esc/src/esc_motor.cpp
    esc/src/esc_protocol.cpp
)
target_include_directories(esc_firmware PUBLIC
    esc/include
)
target_link_libraries(esc_firmware PUBLIC hal_common)

# ─── BLE firmware library ────────────────────────────────────────────
add_library(ble_firmware STATIC
    ble/src/ble_main.cpp
)
target_include_directories(ble_firmware PUBLIC
    ble/include
)
target_link_libraries(ble_firmware PUBLIC hal_common)

# ─── BMS firmware library ────────────────────────────────────────────
add_library(bms_firmware STATIC
    bms/src/bms_main.cpp
)
target_include_directories(bms_firmware PUBLIC
    bms/include
)
target_link_libraries(bms_firmware PUBLIC hal_common)

# ─── nRF51822 BLE co-processor firmware library ─────────────────────
add_library(nrf51_firmware STATIC
    nrf51822/src/nrf51_main.cpp
)
target_include_directories(nrf51_firmware PUBLIC
    nrf51822/include
)
target_link_libraries(nrf51_firmware PUBLIC hal_common)

# ─── Simulation framework (header-only) ──────────────────────────────
add_library(simulator INTERFACE)
target_include_directories(simulator INTERFACE
    simulator/include
)
target_link_libraries(simulator INTERFACE esc_firmware ble_firmware bms_firmware nrf51_firmware)

# ─── Test executable ─────────────────────────────────────────────────
add_executable(firmware_tests
    tests/test_main.cpp
    tests/test_esc.cpp
    tests/test_ble.cpp
    tests/test_bms.cpp
    tests/test_protocol.cpp
    tests/test_bus_integration.cpp
    tests/test_binary_equivalence.cpp
    tests/test_nrf51822.cpp
    tests/test_new_modules.cpp
)
target_include_directories(firmware_tests PRIVATE
    tests
)
target_link_libraries(firmware_tests PRIVATE simulator)
target_compile_options(firmware_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -O2>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic -O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /O2>
)

# ─── Dashboard chip simulator (no-brick verification) ────────────────
# Runs the real DashApp logic (firmware/dashboard) against a SimChip model.
add_executable(dashboard_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/../dashboard/sim/sim_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../dashboard/sim/sim_dash_hal.cpp
)
target_include_directories(dashboard_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../dashboard/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../dashboard/sim
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ble/include
    ${CMAKE_CURRENT_SOURCE_DIR}/nrf51822/include
)
target_compile_options(dashboard_sim PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /O2>
)

# ─── nRF51 Bluetooth firmware — BLE session simulator ───────────────
add_executable(ble_sim
    ${CMAKE_CURRENT_SOURCE_DIR}/nrf51822/sim/ble_sim.cpp
)
target_link_libraries(ble_sim PRIVATE nrf51_firmware)
target_compile_options(ble_sim PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /O2>
)

# ─── CTest integration ───────────────────────────────────────────────
enable_testing()
add_test(NAME all_firmware_tests COMMAND firmware_tests)
add_test(NAME dashboard_sim
         COMMAND dashboard_sim
                 ${CMAKE_CURRENT_SOURCE_DIR}/../dashboard/build/dashboard_app.bin)
add_test(NAME ble_sim COMMAND ble_sim)
