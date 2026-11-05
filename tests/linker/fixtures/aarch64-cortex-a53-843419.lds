OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : {
    *(.text.positive)
    *(.text.cascade)
    *(.text.optional)
    *(.text.page_last)
    *(.text.write_destination)
    *(.text.negative)
    *(.text.embedded_data)
  }
  . = ALIGN(0x1000);
  .rela.dyn : { *(.rela .rela*) }
  .data : { *(.data*) }
}
