# ─────────────────────────────────────────────────────────────────────────────
#  CMakeLists.txt — Top-level build for Ninebot G30 Custom Bootloaders
# ─────────────────────────────────────────────────────────────────────────────
#
#  This CMake project configures cross-compilation for ARM Cortex-M targets.
#  It is provided as an alternative to the per-target Makefiles.
#
#  Usage:
#    # BLE Dashboard STM32 bootloader
#    cmake -B build/ble -DTARGET_BOARD=ble \
#          -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m3.cmake
#    cmake --build build/ble
#
#    # BMS STM32 bootloader
#    cmake -B build/bms -DTARGET_BOARD=bms \
#          -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m3.cmake
#    cmake --build build/bms
#
#    # nRF51822 bootloader
#    cmake -B build/nrf51 -DTARGET_BOARD=nrf51 \
#          -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m0.cmake
#    cmake --build build/nrf51
#
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.20)

project(ninebot_g30_bootloader
    VERSION 1.0.0
    LANGUAGES C ASM
    DESCRIPTION "Secure bootloader for Ninebot G30 Max"
)

# ── Target board selection ──────────────────────────────────────────────────
if(NOT DEFINED TARGET_BOARD)
    message(FATAL_ERROR
        "TARGET_BOARD not set. Use -DTARGET_BOARD=ble|bms|nrf51\n"
        "  ble   = BLE Dashboard STM32F103C8T6\n"
        "  bms   = BMS STM32F103C8T6\n"
        "  nrf51 = nRF51822-QFAA"
    )
endif()

message(STATUS "Target board: ${TARGET_BOARD}")

# ── Common sources (shared crypto, protocol, image validation) ──────────────
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set(COMMON_SOURCES
    ${COMMON_DIR}/src/sha256.c
    ${COMMON_DIR}/src/ecdsa.c
    ${COMMON_DIR}/src/xmodem.c
    ${COMMON_DIR}/src/crc32.c
    ${COMMON_DIR}/src/fw_header.c
)
set(COMMON_INCLUDE ${COMMON_DIR}/include)

# ── Per-target configuration ────────────────────────────────────────────────

if(TARGET_BOARD STREQUAL "ble" OR TARGET_BOARD STREQUAL "bms")
    # ── STM32F103C8T6 bootloader (BLE or BMS) ──────────────────────────
    set(STM32_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stm32)

    set(TARGET_SOURCES
        ${STM32_DIR}/src/bootloader_main.c
        ${STM32_DIR}/src/stm32_flash.c
        ${STM32_DIR}/src/stm32_uart.c
        ${STM32_DIR}/src/startup.s
    )
    set(TARGET_INCLUDE ${STM32_DIR}/include)
    set(LINKER_SCRIPT ${STM32_DIR}/stm32f103c8_bootloader.ld)

    set(MCU_FLAGS -mcpu=cortex-m3 -mthumb)

    if(TARGET_BOARD STREQUAL "ble")
        set(TARGET_DEFINE "TARGET_BOARD=1")  # BOARD_BLE_STM32
        set(OUTPUT_NAME "ble_bootloader")
    else()
        set(TARGET_DEFINE "TARGET_BOARD=2")  # BOARD_BMS_STM32
        set(OUTPUT_NAME "bms_bootloader")
    endif()

elseif(TARGET_BOARD STREQUAL "nrf51")
    # ── nRF51822-QFAA bootloader ────────────────────────────────────────
    set(NRF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nrf51)

    set(TARGET_SOURCES
        ${NRF_DIR}/src/bootloader_main.c
        ${NRF_DIR}/src/nrf51_flash.c
        ${NRF_DIR}/src/nrf51_uart.c
        ${NRF_DIR}/src/startup.s
    )
    set(TARGET_INCLUDE ${NRF_DIR}/include)
    set(LINKER_SCRIPT ${NRF_DIR}/nrf51822_bootloader.ld)

    set(MCU_FLAGS -mcpu=cortex-m0 -mthumb)
    set(TARGET_DEFINE "TARGET_BOARD=3")  # nRF51822
    set(OUTPUT_NAME "nrf51_bootloader")

else()
    message(FATAL_ERROR "Unknown TARGET_BOARD: ${TARGET_BOARD}")
endif()

# ── Executable ──────────────────────────────────────────────────────────────

add_executable(${OUTPUT_NAME}
    ${COMMON_SOURCES}
    ${TARGET_SOURCES}
)

target_include_directories(${OUTPUT_NAME} PRIVATE
    ${COMMON_INCLUDE}
    ${TARGET_INCLUDE}
)

target_compile_definitions(${OUTPUT_NAME} PRIVATE
    ${TARGET_DEFINE}
)

# ── Compiler flags ──────────────────────────────────────────────────────────

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

target_compile_options(${OUTPUT_NAME} PRIVATE
    ${MCU_FLAGS}
    -Os
    -Wall
    -Wextra
    -Werror
    -Wno-unused-variable
    -Wno-unused-function
    -ffunction-sections
    -fdata-sections
    -fno-builtin
    -fno-exceptions
    -ffreestanding
)

target_link_options(${OUTPUT_NAME} PRIVATE
    ${MCU_FLAGS}
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.map
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    -lgcc
)

# ── Post-build: generate .bin and .hex ──────────────────────────────────────

add_custom_command(TARGET ${OUTPUT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:${OUTPUT_NAME}>
        ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:${OUTPUT_NAME}>
        ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${OUTPUT_NAME}>
    COMMENT "Generating ${OUTPUT_NAME}.bin and ${OUTPUT_NAME}.hex"
)

# ── Print build summary ────────────────────────────────────────────────────

message(STATUS "──────────────────────────────────────")
message(STATUS "  Target:        ${OUTPUT_NAME}")
message(STATUS "  Board:         ${TARGET_BOARD}")
message(STATUS "  MCU flags:     ${MCU_FLAGS}")
message(STATUS "  Linker script: ${LINKER_SCRIPT}")
message(STATUS "──────────────────────────────────────")
