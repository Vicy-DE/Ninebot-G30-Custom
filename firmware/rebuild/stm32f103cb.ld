/*
 * Linker script for Ninebot G30 firmware reassembly
 *
 * Target: STM32F103CBT6 (128KB Flash, 20KB SRAM)
 *
 * NOTE: This places all content at 0x08001000 (application area).
 * The bootloader occupies 0x08000000 - 0x08000FFF.
 */

MEMORY
{
    /* Application flash: starts after 4KB bootloader */
    FLASH (rx) : ORIGIN = 0x08001000, LENGTH = 124K

    /* SRAM */
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS
{
    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > FLASH

    /* Discard everything else - this is a flat binary reassembly */
    /DISCARD/ :
    {
        *(.data)
        *(.bss)
        *(.comment)
        *(.ARM.attributes)
        *(.ARM.exidx)
    }
}
