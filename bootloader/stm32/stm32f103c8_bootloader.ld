/*
 * Linker script for STM32F103C8T6 secure bootloader.
 * Target: BLE Dashboard + BMS (both 64 KB flash, 20 KB SRAM)
 *
 * Bootloader region: 0x08000000 — 0x08003FFF (16 KB)
 * Application region starts at 0x08004000 (managed by bootloader, not linked here)
 */

MEMORY
{
    /* Bootloader flash: first 16 KB */
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 16K

    /* Full SRAM available to bootloader */
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Entry point */
ENTRY(Reset_Handler)

SECTIONS
{
    /* Vector table — must be at the very start of flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Code and read-only data */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Initialized data — stored in flash, copied to RAM at startup */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    /* Zero-initialized data */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Stack (grows downward from top of RAM) */
    ._stack :
    {
        . = ALIGN(8);
        _end = .;
        . = . + 2048;   /* 2 KB min stack */
        _estack = .;
    } > RAM

    /* Discard debug info during embedded build */
    /DISCARD/ :
    {
        *(.ARM.exidx)
        *(.ARM.attributes)
        *(.comment)
        *(.note.*)
    }
}

/* Verify bootloader fits in 16 KB */
ASSERT(SIZEOF(.text) + SIZEOF(.isr_vector) + SIZEOF(.data) <= 16K,
       "Bootloader exceeds 16 KB flash limit!")
