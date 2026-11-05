OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .rodata : { *(.rodata.str1.8) }
}
