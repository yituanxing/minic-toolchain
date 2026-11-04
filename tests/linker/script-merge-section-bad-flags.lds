OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { *(.rodata .rodata.tls) }
  /DISCARD/ : { *(.rodata.mutable) *(.rodata.nobits) }
}
