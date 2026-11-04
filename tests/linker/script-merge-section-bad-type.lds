OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .rodata : { *(.rodata .rodata.nobits) }
  /DISCARD/ : { *(.rodata.mutable) *(.rodata.tls) }
}
