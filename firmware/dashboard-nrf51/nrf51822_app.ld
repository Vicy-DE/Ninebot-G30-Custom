/* Linker script — nRF51822-QFAA app slot, above the Nordic S110 SoftDevice.
 *
 * Layout confirmed from the stock dump:
 *   0x00000000 .. 0x00017FFF   S110 SoftDevice   (UICR.CLENR0 = 0x18000)
 *   0x00018000 .. 0x0003BFFF   application       <- this image
 *   0x0003C000 .. 0x0003FFFF   bootloader        (UICR.BOOTLOADERADDR)
 *
 * RAM: 16 KB part (stock initial SP was 0x20003DE8). The SoftDevice owns the low RAM,
 * so the app starts at 0x20002000.
 */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00018000, LENGTH = 0x24000  /* 144K, stops before the bootloader */
  RAM  (rwx) : ORIGIN = 0x20002000, LENGTH = 0x2000   /* 8K above the SoftDevice */
}

ENTRY(Reset_Handler)

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))          /* must be first: SP + reset vector at 0x00018000 */
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  _sidata = LOADADDR(.data);

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  . = ALIGN(4);
  _end = .;
  PROVIDE(end = _end);          /* newlib's _sbrk expects `end` */

  _estack = ORIGIN(RAM) + LENGTH(RAM);

  /DISCARD/ : { *(.ARM.exidx*) *(.eh_frame*) }
}
