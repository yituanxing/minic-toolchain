ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .rodata : { *(.rodata .rodata.* .data..ro_after_init) }
}
