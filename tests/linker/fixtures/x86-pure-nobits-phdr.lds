OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);
  bss PT_LOAD FLAGS(6);
}
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) } :text
  . = 0x21008;
  .bss : { *(.bss.payload) } :bss
}
