OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text.pad_trigger) }
  .pad : { . = ALIGN(16); }
  . = ALIGN(0x1000);
  .data : { *(.data) }
}
