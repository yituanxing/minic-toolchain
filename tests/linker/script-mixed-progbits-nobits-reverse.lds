ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  .init.pi : { *(.init.pi.bss) *(.init.pi.data) } =0x7e
  .tail : { *(.tail.data) }
}
