ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .init.pi : { *(.init.pi.data) *(.init.pi.bss) } =0x7e
  . = ALIGN(0x1000);
  .tail : { *(.tail.data) }
}
