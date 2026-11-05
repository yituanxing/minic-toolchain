OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { *(.rodata .rodata.note) }
  /DISCARD/ : { *(.rodata.mutable) *(.rodata.tls) *(.rodata.nobits) }
}
