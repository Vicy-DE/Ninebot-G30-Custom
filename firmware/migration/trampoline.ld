/* App1 — trampoline, booted by the OLD 4 KB bootloader at 0x08001000.
 * Tiny: it only jumps to the new app/updater at 0x08004000. Kept under 12 KB so
 * it can never overlap 0x08004000. */
_vtor_base = 0x08001000;
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08001000, LENGTH = 12K   /* 0x08001000 .. 0x08003FFF */
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : { *(.text*) *(.rodata*) *(.glue_7*) . = ALIGN(4); _etext = .; } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.ramfunc*) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    .bss : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    ._stack : { . = ALIGN(8); _end = .; . = . + 1024; _estack = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
ASSERT(_etext <= 0x08004000, "trampoline must not reach the app slot at 0x08004000")
