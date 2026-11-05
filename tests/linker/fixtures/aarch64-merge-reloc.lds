OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .rodata : { *(.rodata.str1.8) }
  . = ALIGN(0x1000);
  .data : { *(.data) }
}
