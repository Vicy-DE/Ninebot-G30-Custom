/* Bootloader installer — runs at 0x08008000 (the "32 offset", the app slot that
 * the relocated bootloader at 0x08004000 flashes to). From here it erases+writes
 * the bootloader region at 0x08004000 (the "16 offset") without overlapping its
 * own code. The bootloader image to install is .incbin'd into .bl_image.
 *
 * Reuses firmware/migration/src/{startup.s, updater.cpp, flash_rt.c, newbl.s};
 * updater.cpp is compiled with -DMIG_BL_BASE=0x08004000u so it targets the 16 offset. */
_vtor_base = 0x08008000;
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 30K   /* app slot at the 32 offset */
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : { *(.text*) *(.rodata*) *(.glue_7*) . = ALIGN(4); _etext = .; } > FLASH
    /* embedded bootloader image, read-only, in the app region (NOT 0x08004000) */
    .bl_image : { . = ALIGN(4); KEEP(*(.bl_image)) . = ALIGN(4); } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.ramfunc*) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    .bss : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    ._stack : { . = ALIGN(8); _end = .; . = . + 2048; _estack = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
