/**
 * @file nrf51822_bootloader.ld
 * @brief Linker script for nRF51822-QFAA bootloader.
 *
 * Memory layout:
 *   0x00000000 - 0x00000FFF  MBR (4 KB, Nordic Master Boot Record)
 *   0x00001000 - 0x00017FFF  SoftDevice S110 v8.0 (92 KB)
 *   0x00018000 - 0x0003BFFF  Application (144 KB — but we limit to 80 KB)
 *   0x0003C000 - 0x0003FFFF  Bootloader (16 KB code)
 *   0x10001000 - 0x10001013  UICR (User Info Config Registers)
 *
 * nRF51822-QFAA: 256 KB flash, 16 KB SRAM
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x0003C000, LENGTH = 16K   /* Bootloader code */
    RAM (rwx)   : ORIGIN = 0x20002000, LENGTH = 8K    /* Upper half of 16KB SRAM */
}

/*
 * RAM note: SoftDevice S110 v8.0 uses the lower 8 KB of SRAM.
 * Bootloader gets upper 8 KB (0x20002000 - 0x20004000).
 * If SoftDevice is not active during bootloader operation, the full 16 KB
 * could be used (change ORIGIN to 0x20000000, LENGTH to 16K).
 */

ENTRY(Reset_Handler)

SECTIONS
{
    /* ── Interrupt vector table ─────────────────────────────────────── */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* ── Code ───────────────────────────────────────────────────────── */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ── Initialized data (load address in FLASH, VMA in RAM) ───── */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH

    /* ── Zero-initialized data ──────────────────────────────────────── */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* ── Stack (grows downward from top of RAM) ─────────────────── */
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);

    /* ── Size checks ────────────────────────────────────────────── */
    ASSERT(SIZEOF(.text) + SIZEOF(.isr_vector) + SIZEOF(.data) <= 16K,
           "ERROR: Bootloader code exceeds 16 KB flash region!")
    ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= 8K,
           "ERROR: Bootloader RAM usage exceeds 8 KB!")
}
