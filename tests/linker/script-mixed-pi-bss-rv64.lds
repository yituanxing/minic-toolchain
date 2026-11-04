OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .init.pi : {
    *(.init.pi.code)
    *(.init.pi.bss)
    *(.init.pi.tail)
  }
}
