ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  .rodata : { *(.rodata .rodata.* .data..ro_after_init) }
}
