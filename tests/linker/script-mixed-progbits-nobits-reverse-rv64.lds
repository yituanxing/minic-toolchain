OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .init.pi : { *(.init.pi.bss) *(.init.pi.data) } =0x7e
  .tail : { *(.tail.data) }
}
