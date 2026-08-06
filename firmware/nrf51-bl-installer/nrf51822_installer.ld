/* nRF51 bootloader installer — runs from the APPLICATION slot, writes the bootloader slot.
 * The erase+program loop lives in .ramfunc so the window never depends on flash reads.
 */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00018000, LENGTH = 0x24000
  RAM  (rwx) : ORIGIN = 0x20002000, LENGTH = 0x2000
}
ENTRY(Reset_Handler)
SECTIONS
{
  .text : { KEEP(*(.isr_vector)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .data :
  {
    . = ALIGN(4); _sdata = .;
    *(.ramfunc) *(.ramfunc*)      /* RAM-resident installer, copied by startup with .data */
    . = ALIGN(4);
    *(.data*)
    . = ALIGN(4); _edata = .;
  } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  . = ALIGN(4); _end = .; PROVIDE(end = _end);
  _stack_top = ORIGIN(RAM) + LENGTH(RAM);
  _estack = _stack_top;
  /DISCARD/ : { *(.ARM.exidx*) *(.eh_frame*) }
}
