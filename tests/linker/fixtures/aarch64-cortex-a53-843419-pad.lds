OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text.pad_trigger) }
  /* Empty before erratum planning, materialized by the full-page veneer. */
  .pad : { . += (SIZEOF(.text) - 0x1010) / 512; }
  . = ALIGN(0x1000);
  .data : { *(.data) }
}
