#!/usr/bin/env python3
"""
update_bootloader.py — Update an existing custom bootloader on a running board.

This tool creates a "bootloader updater" firmware image that can be flashed
via the custom bootloader's normal XMODEM update path. The updater:

  1. Gets flashed as a normal application via XMODEM + ECDSA verification
  2. On first boot, copies the new bootloader from its embedded data to SRAM
  3. Erases the bootloader flash region (pages 0-15 on STM32, or 0x3C000+ on nRF51)
  4. Writes the new bootloader from SRAM to flash
  5. Verifies CRC-32
  6. Resets into the new bootloader

This is essentially the same as initial_flash.py but it produces a .sfw image
that can be sent via the custom bootloader's XMODEM-CRC update path.

Flow:
  PC → xmodem_send.py → VESC passthrough → board bootloader (XMODEM)
       → bootloader verifies .sfw signature
       → writes updater app to app region
       → boots updater app
       → updater overwrites bootloader region
       → resets into new bootloader

Usage:
    # Step 1: Generate the updater .sfw image
    python update_bootloader.py \\
        --board ble \\
        --new-bootloader build/ble_bootloader.bin \\
        --signing-key keys/private_key.pem \\
        --output ble_bl_update.sfw

    # Step 2: Send via XMODEM to the running bootloader
    python xmodem_send.py --port COM3 --file ble_bl_update.sfw --trigger -t ble-stm32

    # Step 3: The board reboots into the updater, updates bootloader, reboots again

For nRF51822:
    python update_bootloader.py \\
        --board nrf51 \\
        --new-bootloader build/nrf51_bootloader.bin \\
        --signing-key keys/private_key.pem \\
        --output nrf51_bl_update.sfw
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib
from pathlib import Path

# Add parent directory to path for sign_firmware module
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sign_firmware import sign_firmware, TARGET_IDS, SFW_HEADER_SIZE


# ── Board configurations ───────────────────────────────────────────────────

BOARD_CONFIG = {
    "ble": {
        "target": "ble-stm32",
        "mcu": "cortex-m3",
        "bootloader_addr": 0x08000000,
        "bootloader_size": 16 * 1024,
        "app_addr": 0x08004000,
        "page_size": 1024,
        "sram_base": 0x20000000,
        "sram_size": 20 * 1024,
        "stack_top": 0x20005000,
        "flash_reg": 0x40022000,
    },
    "bms": {
        "target": "bms-stm32",
        "mcu": "cortex-m3",
        "bootloader_addr": 0x08000000,
        "bootloader_size": 16 * 1024,
        "app_addr": 0x08004000,
        "page_size": 1024,
        "sram_base": 0x20000000,
        "sram_size": 20 * 1024,
        "stack_top": 0x20005000,
        "flash_reg": 0x40022000,
    },
    "nrf51": {
        "target": "nrf51822",
        "mcu": "cortex-m0",
        "bootloader_addr": 0x0003C000,
        "bootloader_size": 16 * 1024,
        "app_addr": 0x00018000,
        "page_size": 1024,
        "sram_base": 0x20000000,
        "sram_size": 16 * 1024,
        "stack_top": 0x20004000,
        "flash_reg": 0x4001E000,  # NVMC
    },
}


def generate_updater_source(board: str, bootloader_data: bytes) -> dict:
    """
    Generate C source files for the bootloader updater application.

    Returns a dict of filename -> content.
    """
    cfg = BOARD_CONFIG[board]
    bl_size = len(bootloader_data)
    bl_padded = bootloader_data.ljust(cfg["bootloader_size"], b'\xFF')
    bl_crc = zlib.crc32(bl_padded) & 0xFFFFFFFF
    num_pages = cfg["bootloader_size"] // cfg["page_size"]

    # Generate C array
    c_lines = []
    for i in range(0, bl_size, 16):
        chunk = bootloader_data[i:i+16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        c_lines.append(f"    {hex_vals},")
    bl_c_array = "\n".join(c_lines)

    files = {}

    if cfg["mcu"] == "cortex-m3":
        # ── STM32 updater ───────────────────────────────────────────────
        files["updater.c"] = f"""\
/**
 * @file updater.c
 * @brief Bootloader self-updater for {board.upper()} STM32F103.
 *
 * Runs from app region (0x{cfg['app_addr']:08X}), copies new bootloader
 * binary to SRAM, erases bootloader flash region, writes new bootloader,
 * verifies CRC-32, and resets.
 */

#include <stdint.h>

#define FLASH_REG_BASE     0x{cfg['flash_reg']:08X}U
#define FLASH_KEYR         (*(volatile uint32_t *)(FLASH_REG_BASE + 0x04U))
#define FLASH_SR           (*(volatile uint32_t *)(FLASH_REG_BASE + 0x0CU))
#define FLASH_CR           (*(volatile uint32_t *)(FLASH_REG_BASE + 0x10U))
#define FLASH_AR           (*(volatile uint32_t *)(FLASH_REG_BASE + 0x14U))
#define FLASH_KEY1         0x45670123U
#define FLASH_KEY2         0xCDEF89ABU
#define SCB_AIRCR          (*(volatile uint32_t *)0xE000ED0CU)

#define BOOTLOADER_ADDR    0x{cfg['bootloader_addr']:08X}U
#define BOOTLOADER_SIZE    {cfg['bootloader_size']}U
#define BOOTLOADER_PAGES   {num_pages}U
#define PAGE_SIZE          {cfg['page_size']}U
#define BL_DATA_SIZE       {bl_size}U
#define EXPECTED_CRC32     0x{bl_crc:08X}U

static const uint8_t new_bootloader[{bl_size}] = {{
{bl_c_array}
}};

static uint8_t sram_buf[BOOTLOADER_SIZE] __attribute__((section(".bl_buffer")));

static uint32_t crc32(const uint8_t *d, uint32_t n) {{
    uint32_t c = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < n; i++) {{
        c ^= d[i];
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320U : c >> 1;
    }}
    return ~c;
}}

__attribute__((section(".ramfunc"), noinline, long_call))
static void do_flash_update(void) {{
    /* Unlock */
    if (*(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) & (1U << 7)) {{
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x04U) = 0x45670123U;
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x04U) = 0xCDEF89ABU;
    }}

    /* Erase pages */
    for (uint32_t p = 0; p < BOOTLOADER_PAGES; p++) {{
        while (*(volatile uint32_t *)(FLASH_REG_BASE + 0x0CU) & 1U) {{}}
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) |= (1U << 1);
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x14U) = BOOTLOADER_ADDR + p * PAGE_SIZE;
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) |= (1U << 6);
        while (*(volatile uint32_t *)(FLASH_REG_BASE + 0x0CU) & 1U) {{}}
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) &= ~(1U << 1);
    }}

    /* Write half-words */
    for (uint32_t i = 0; i < BOOTLOADER_SIZE; i += 2) {{
        uint16_t hw;
        if (i < BL_DATA_SIZE)
            hw = (uint16_t)sram_buf[i] | ((uint16_t)sram_buf[i+1] << 8);
        else
            hw = 0xFFFF;
        while (*(volatile uint32_t *)(FLASH_REG_BASE + 0x0CU) & 1U) {{}}
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) |= (1U << 0);
        *(volatile uint16_t *)(BOOTLOADER_ADDR + i) = hw;
        while (*(volatile uint32_t *)(FLASH_REG_BASE + 0x0CU) & 1U) {{}}
        *(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) &= ~(1U << 0);
    }}

    /* Lock */
    *(volatile uint32_t *)(FLASH_REG_BASE + 0x10U) |= (1U << 7);

    /* Reset */
    __asm volatile ("dsb 0xF":::"memory");
    *(volatile uint32_t *)0xE000ED0CU = 0x05FA0004U;
    for (;;) {{}}
}}

int main(void) {{
    /* Copy to SRAM */
    for (uint32_t i = 0; i < BL_DATA_SIZE; i++)
        sram_buf[i] = new_bootloader[i];
    for (uint32_t i = BL_DATA_SIZE; i < BOOTLOADER_SIZE; i++)
        sram_buf[i] = 0xFF;

    /* Verify SRAM copy */
    if (crc32(sram_buf, BOOTLOADER_SIZE) != EXPECTED_CRC32)
        for (;;) {{ __asm volatile("nop"); }}

    /* Flash from SRAM */
    do_flash_update();
    for (;;) {{}}
}}
"""

        files["startup.s"] = f"""\
    .syntax unified
    .cpu cortex-m3
    .thumb
    .equ STACK_TOP, 0x{cfg['stack_top']:08X}

    .section .isr_vector, "a", %progbits
g_vectors:
    .word STACK_TOP
    .word Reset_Handler
    .rept 14
    .word Default_Handler
    .endr
    .rept 59
    .word Default_Handler
    .endr

    .section .text
    .global Reset_Handler
    .thumb_func
Reset_Handler:
    ldr r0, =STACK_TOP
    msr msp, r0
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
1:  cmp r0, r1
    bge 2f
    str r2, [r0]
    adds r0, #4
    b 1b
2:  ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
3:  cmp r0, r1
    bge 4f
    ldr r3, [r2]
    str r3, [r0]
    adds r0, #4
    adds r2, #4
    b 3b
4:  ldr r0, =_sramfunc
    ldr r1, =_eramfunc
    ldr r2, =_siramfunc
5:  cmp r0, r1
    bge 6f
    ldr r3, [r2]
    str r3, [r0]
    adds r0, #4
    adds r2, #4
    b 5b
6:  bl main
    b .

    .weak Default_Handler
    .thumb_func
Default_Handler:
    b .
    .end
"""

        files["updater.ld"] = f"""\
/* Linker script for bootloader updater — runs from custom app region */
MEMORY
{{
    FLASH (rx) : ORIGIN = 0x{cfg['app_addr']:08X}, LENGTH = 46K
    RAM (rwx)  : ORIGIN = 0x{cfg['sram_base']:08X}, LENGTH = {cfg['sram_size'] // 1024}K
}}
ENTRY(Reset_Handler)
SECTIONS
{{
    .isr_vector : {{ KEEP(*(.isr_vector)) }} > FLASH
    .text : {{ *(.text*) *(.rodata*) _etext = .; }} > FLASH
    _siramfunc = LOADADDR(.ramfunc);
    .ramfunc : {{ _sramfunc = .; *(.ramfunc*) _eramfunc = .; }} > RAM AT> FLASH
    _sidata = LOADADDR(.data);
    .data : {{ _sdata = .; *(.data*) _edata = .; }} > RAM AT> FLASH
    .bss : {{ _sbss = .; *(.bss*) *(.bl_buffer) *(COMMON) _ebss = .; }} > RAM
    /DISCARD/ : {{ *(.ARM.exidx) *(.ARM.attributes) *(.comment) }}
}}
"""

    elif cfg["mcu"] == "cortex-m0":
        # ── nRF51822 updater ────────────────────────────────────────────
        files["updater.c"] = f"""\
/**
 * @file updater.c
 * @brief Bootloader self-updater for nRF51822.
 *
 * Runs from app region (0x{cfg['app_addr']:08X}), erases bootloader region
 * at 0x{cfg['bootloader_addr']:08X}, writes new bootloader, verifies, resets.
 *
 * The nRF51822 NVMC does word-aligned writes (4 bytes).
 */

#include <stdint.h>

#define NVMC_BASE          0x4001E000U
#define NVMC_READY         (*(volatile uint32_t *)(NVMC_BASE + 0x400U))
#define NVMC_CONFIG        (*(volatile uint32_t *)(NVMC_BASE + 0x504U))
#define NVMC_ERASEPAGE     (*(volatile uint32_t *)(NVMC_BASE + 0x508U))

#define NVMC_CONFIG_WEN    1U
#define NVMC_CONFIG_EEN    2U
#define NVMC_CONFIG_REN    0U

#define POWER_RESETREAS    (*(volatile uint32_t *)0x40000400U)
#define POWER_GPREGRET     (*(volatile uint32_t *)0x4000051CU)

#define BOOTLOADER_ADDR    0x{cfg['bootloader_addr']:08X}U
#define BOOTLOADER_SIZE    {cfg['bootloader_size']}U
#define BOOTLOADER_PAGES   {num_pages}U
#define PAGE_SIZE          {cfg['page_size']}U
#define BL_DATA_SIZE       {bl_size}U
#define EXPECTED_CRC32     0x{bl_crc:08X}U

static const uint8_t new_bootloader[{bl_size}] = {{
{bl_c_array}
}};

static uint8_t sram_buf[BOOTLOADER_SIZE] __attribute__((section(".bl_buffer")));

static uint32_t crc32(const uint8_t *d, uint32_t n) {{
    uint32_t c = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < n; i++) {{
        c ^= d[i];
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320U : c >> 1;
    }}
    return ~c;
}}

__attribute__((section(".ramfunc"), noinline, long_call))
static void do_flash_update(void) {{
    /* Erase bootloader pages */
    NVMC_CONFIG = NVMC_CONFIG_EEN;
    while (!NVMC_READY) {{}}
    for (uint32_t p = 0; p < BOOTLOADER_PAGES; p++) {{
        NVMC_ERASEPAGE = BOOTLOADER_ADDR + p * PAGE_SIZE;
        while (!NVMC_READY) {{}}
    }}

    /* Write words */
    NVMC_CONFIG = NVMC_CONFIG_WEN;
    while (!NVMC_READY) {{}}
    for (uint32_t i = 0; i < BOOTLOADER_SIZE; i += 4) {{
        uint32_t w;
        if (i + 3 < BL_DATA_SIZE) {{
            w = (uint32_t)sram_buf[i]
              | ((uint32_t)sram_buf[i+1] << 8)
              | ((uint32_t)sram_buf[i+2] << 16)
              | ((uint32_t)sram_buf[i+3] << 24);
        }} else if (i < BL_DATA_SIZE) {{
            w = 0xFFFFFFFFU;
            for (uint32_t b = 0; b < 4 && (i+b) < BL_DATA_SIZE; b++)
                w = (w & ~(0xFFU << (b*8))) | ((uint32_t)sram_buf[i+b] << (b*8));
        }} else {{
            w = 0xFFFFFFFFU;
        }}
        *(volatile uint32_t *)(BOOTLOADER_ADDR + i) = w;
        while (!NVMC_READY) {{}}
    }}

    NVMC_CONFIG = NVMC_CONFIG_REN;
    while (!NVMC_READY) {{}}

    /* Reset via AIRCR */
    *(volatile uint32_t *)0xE000ED0CU = 0x05FA0004U;
    for (;;) {{}}
}}

int main(void) {{
    for (uint32_t i = 0; i < BL_DATA_SIZE; i++)
        sram_buf[i] = new_bootloader[i];
    for (uint32_t i = BL_DATA_SIZE; i < BOOTLOADER_SIZE; i++)
        sram_buf[i] = 0xFF;

    if (crc32(sram_buf, BOOTLOADER_SIZE) != EXPECTED_CRC32)
        for (;;) {{}}

    do_flash_update();
    for (;;) {{}}
}}
"""

        files["startup.s"] = f"""\
    .syntax unified
    .cpu cortex-m0
    .thumb
    .equ STACK_TOP, 0x{cfg['stack_top']:08X}

    .section .isr_vector, "a", %progbits
g_vectors:
    .word STACK_TOP
    .word Reset_Handler
    .rept 14
    .word Default_Handler
    .endr
    .rept 26
    .word Default_Handler
    .endr

    .section .text
    .global Reset_Handler
    .thumb_func
Reset_Handler:
    ldr r0, =STACK_TOP
    msr msp, r0
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
1:  cmp r0, r1
    bge 2f
    str r2, [r0]
    adds r0, #4
    b 1b
2:  ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
3:  cmp r0, r1
    bge 4f
    ldr r3, [r2]
    str r3, [r0]
    adds r0, #4
    adds r2, #4
    b 3b
4:  ldr r0, =_sramfunc
    ldr r1, =_eramfunc
    ldr r2, =_siramfunc
5:  cmp r0, r1
    bge 6f
    ldr r3, [r2]
    str r3, [r0]
    adds r0, #4
    adds r2, #4
    b 5b
6:  bl main
    b .

    .weak Default_Handler
    .thumb_func
Default_Handler:
    b .
    .end
"""

        files["updater.ld"] = f"""\
/* Linker script for nRF51822 bootloader updater — runs from app region */
MEMORY
{{
    FLASH (rx) : ORIGIN = 0x{cfg['app_addr']:08X}, LENGTH = 80K
    RAM (rwx)  : ORIGIN = 0x20002000, LENGTH = 8K
}}
ENTRY(Reset_Handler)
SECTIONS
{{
    .isr_vector : {{ KEEP(*(.isr_vector)) }} > FLASH
    .text : {{ *(.text*) *(.rodata*) _etext = .; }} > FLASH
    _siramfunc = LOADADDR(.ramfunc);
    .ramfunc : {{ _sramfunc = .; *(.ramfunc*) _eramfunc = .; }} > RAM AT> FLASH
    _sidata = LOADADDR(.data);
    .data : {{ _sdata = .; *(.data*) _edata = .; }} > RAM AT> FLASH
    .bss : {{ _sbss = .; *(.bss*) *(.bl_buffer) *(COMMON) _ebss = .; }} > RAM
    /DISCARD/ : {{ *(.ARM.exidx) *(.ARM.attributes) *(.comment) }}
}}
"""

    # ── Makefile (common for both) ──────────────────────────────────────
    cpu = "cortex-m3" if cfg["mcu"] == "cortex-m3" else "cortex-m0"
    files["Makefile"] = f"""\
PREFIX  ?= arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

BUILD    = build
TARGET   = $(BUILD)/updater

CFLAGS   = -mcpu={cpu} -mthumb -Os -Wall -std=c11
CFLAGS  += -ffunction-sections -fdata-sections -fno-builtin -nostdlib
LDFLAGS  = -mcpu={cpu} -mthumb -Tupdater.ld
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(TARGET).map -nostdlib --specs=nosys.specs

.PHONY: all clean
all: $(BUILD) $(TARGET).bin
\t@$(SIZE) $(TARGET).elf

$(BUILD):
\tmkdir -p $(BUILD)

$(BUILD)/updater.o: updater.c
\t$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/startup.o: startup.s
\t$(CC) -mcpu={cpu} -mthumb -x assembler-with-cpp -c $< -o $@

$(TARGET).elf: $(BUILD)/updater.o $(BUILD)/startup.o
\t$(CC) $(LDFLAGS) $^ -o $@

$(TARGET).bin: $(TARGET).elf
\t$(OBJCOPY) -O binary $< $@

clean:
\trm -rf $(BUILD)
"""

    return files


def main():
    parser = argparse.ArgumentParser(
        description="Generate bootloader self-update firmware image."
    )
    parser.add_argument("--board", "-b", required=True, choices=BOARD_CONFIG.keys())
    parser.add_argument("--new-bootloader", "-B", required=True,
                        help="New bootloader binary (.bin)")
    parser.add_argument("--signing-key", "-k", 
                        help="ECDSA private key (.pem) for signing the updater as .sfw")
    parser.add_argument("--output", "-o", help="Output file (.sfw or directory)")
    parser.add_argument("--version", "-v", default="0.0.1",
                        help="Version for the updater .sfw image")
    parser.add_argument("--build", action="store_true",
                        help="Compile the updater (requires arm-none-eabi-gcc)")

    args = parser.parse_args()

    if not os.path.isfile(args.new_bootloader):
        print(f"ERROR: Bootloader not found: {args.new_bootloader}")
        sys.exit(1)

    with open(args.new_bootloader, "rb") as f:
        bl_data = f.read()

    cfg = BOARD_CONFIG[args.board]
    if len(bl_data) > cfg["bootloader_size"]:
        print(f"ERROR: Bootloader too large: {len(bl_data)} > {cfg['bootloader_size']}")
        sys.exit(1)

    # Determine output directory
    out_dir = args.output if args.output and not args.output.endswith('.sfw') \
              else f"updater-{args.board}"
    os.makedirs(out_dir, exist_ok=True)

    print(f"Generating bootloader updater for {args.board.upper()}...")
    print(f"  New bootloader: {args.new_bootloader} ({len(bl_data)} bytes)")
    print(f"  Output dir:     {out_dir}/")
    print()

    # Generate source files
    files = generate_updater_source(args.board, bl_data)
    for fname, content in files.items():
        fpath = os.path.join(out_dir, fname)
        with open(fpath, "w", newline="\n") as f:
            f.write(content)
        print(f"  Created: {fpath}")

    print()

    # Optionally build
    if args.build:
        print("Building updater...")
        ret = os.system(f"cd \"{out_dir}\" && make")
        if ret != 0:
            print("ERROR: Build failed")
            sys.exit(1)

        updater_bin = os.path.join(out_dir, "build", "updater.bin")

        # Optionally sign as .sfw
        if args.signing_key:
            print()
            print("Signing updater as .sfw...")
            with open(updater_bin, "rb") as f:
                updater_data = f.read()

            target_id = TARGET_IDS[cfg["target"]]
            version = tuple(int(p) for p in args.version.split("."))

            sfw_image = sign_firmware(
                firmware_data=updater_data,
                private_key_path=args.signing_key,
                target_id=target_id,
                version=version,
            )

            sfw_path = args.output if args.output and args.output.endswith('.sfw') \
                       else os.path.join(out_dir, "build", f"bl_update_{args.board}.sfw")
            with open(sfw_path, "wb") as f:
                f.write(sfw_image)
            print(f"  Signed image: {sfw_path}")
    else:
        print("Next steps:")
        print(f"  1. cd {out_dir}")
        print(f"  2. make")
        print(f"  3. python ../sign_firmware.py -i build/updater.bin "
              f"-k ../../keys/private_key.pem -t {cfg['target']} -v {args.version}")
        print(f"  4. python ../xmodem_send.py -p COM3 -f build/updater.sfw")


if __name__ == "__main__":
    main()
