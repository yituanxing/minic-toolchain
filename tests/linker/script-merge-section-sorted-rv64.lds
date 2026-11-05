OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .rodata : { *(SORT_BY_NAME(.rodata.*) .rodata) }
}
