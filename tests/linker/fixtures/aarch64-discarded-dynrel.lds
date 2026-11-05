OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  .rela.dyn : { *(.rela.dyn) }
  .data : { *(.data.alive) }
  /DISCARD/ : { *(.data.discard) }
}
