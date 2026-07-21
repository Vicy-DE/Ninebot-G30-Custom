/* App2 — bl_updater, runs at 0x08004000 (the app slot, ABOVE the 16 KB
 * bootloader region). From here it can erase+write 0x08000000-0x08003FFF without
 * overlapping its own code. The new bootloader is .incbin'd into .bl_image. */
_vtor_base = 0x08004000;
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 40K   /* app slot */
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : { *(.text*) *(.rodata*) *(.glue_7*) . = ALIGN(4); _etext = .; } > FLASH
    /* embedded signed bootloader image, read-only, in the app region (NOT 0x08000000) */
    .bl_image : { . = ALIGN(4); KEEP(*(.bl_image)) . = ALIGN(4); } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.ramfunc*) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    .bss : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    ._stack : { . = ALIGN(8); _end = .; . = . + 2048; _estack = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
