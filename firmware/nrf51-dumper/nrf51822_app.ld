/* nRF51 dumper — links into the APPLICATION slot, above the S110 SoftDevice.
 * Read-only app: it never programs flash, so it cannot brick the board.
 */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00018000, LENGTH = 0x24000   /* app slot, stops before the bootloader */
  RAM  (rwx) : ORIGIN = 0x20002000, LENGTH = 0x2000    /* above the SoftDevice's RAM */
}

ENTRY(Reset_Handler)

SECTIONS
{
  .text : { KEEP(*(.isr_vector)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss  : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  . = ALIGN(4); _end = .; PROVIDE(end = _end);
  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
  _estack = _stack_top;
  /DISCARD/ : { *(.ARM.exidx*) *(.eh_frame*) }
}
