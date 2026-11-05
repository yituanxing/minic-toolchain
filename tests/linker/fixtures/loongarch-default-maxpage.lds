OUTPUT_ARCH(loongarch)
SECTIONS
{
  . = 0;
  .head : { *(.head) }
  .text : { *(.text) }
  .rodata : { *(.rodata) }
  .data : { *(.data) }
  .bss : { *(.bss) }
  /DISCARD/ : { *(.note.GNU-stack) }
}
