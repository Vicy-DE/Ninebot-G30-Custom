/* Minimal linker for the STM32C542 on-silicon self-test.
 * Flash 256 KB @ 0x08000000, RAM 64 KB @ 0x20000000 (NUCLEO-C542RC).
 * .result is pinned to the start of RAM (0x20000000) so the host reads it via SWD. */
ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : { *(.text*) *(.rodata*) *(.glue_7*) . = ALIGN(4); _etext = .; } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

    /* fixed result slot at 0x20000000 (read back over SWD) */
    .result (NOLOAD) : { . = ALIGN(4); KEEP(*(.result)) } > RAM

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) *(.ramfunc*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    .bss  : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
