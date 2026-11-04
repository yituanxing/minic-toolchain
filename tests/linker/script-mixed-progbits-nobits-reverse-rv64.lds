OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .init.pi : { *(.init.pi.bss) *(.init.pi.data) } =0x7e
  . = ALIGN(0x1000);
  .tail : { *(.tail.data) }
}
