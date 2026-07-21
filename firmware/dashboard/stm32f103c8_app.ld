/*
 * Linker script — Ninebot G30 dashboard app (STM32F103C8T6, 64 KB / 20 KB).
 * Phase 1: app @ 0x08001000, behind the stock 4 KB IAP bootloader.
 * (For Phase 3 behind the custom 16 KB secure bootloader, change ORIGIN to
 *  0x08004000 and LENGTH to 48K.)
 */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08001000, LENGTH = 60K   /* 64K - 4K bootloader */
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.glue_7) *(.glue_7t)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

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

    ._stack :
    {
        . = ALIGN(8);
        _end = .;
        . = . + 2048;          /* 2 KB min stack headroom */
        _estack = .;
    } > RAM

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.attributes)
        *(.comment)
        *(.note.*)
    }
}

ASSERT(SIZEOF(.text) + SIZEOF(.isr_vector) + SIZEOF(.data) <= 60K,
       "Dashboard app exceeds 60 KB flash limit!")
