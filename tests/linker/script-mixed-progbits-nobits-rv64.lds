OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .init.pi : { *(.init.pi.data) *(.init.pi.bss) } =0x7e
  .tail : { *(.tail.data) }
}
