/*
 * Linker script — Infineon TLE9879-2QXA40
 * Flash @ 0x11000000, RAM @ 0x18000018 (IRAM1)
 * NAC/NAD word lives at start of IROM2 (0x1101EFFC)
 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x11000000, LENGTH = 0x1EFFC
  NAC   (r)   : ORIGIN = 0x1101EFFC, LENGTH = 4
  RAM   (rwx) : ORIGIN = 0x18000018, LENGTH = 0x17E8
}

ENTRY(Reset_Handler)

SECTIONS
{
  .text : {
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .nac (ORIGIN(NAC)) : {
    KEEP(*(.nac_nad))
  } > NAC

  _sidata = LOADADDR(.data);

  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  /* Stack grows down from top of IRAM1 */
  _estack = ORIGIN(RAM) + LENGTH(RAM);
}
