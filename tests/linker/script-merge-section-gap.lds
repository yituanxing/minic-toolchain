ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x100);
  .rodata : { *(.rodata .rodata.*) }
}
