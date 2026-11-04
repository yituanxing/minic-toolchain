OUTPUT_ARCH(i386:x86-64)
ENTRY(_start:invalid)
SECTIONS {
  .text : { *(.text) }
}
