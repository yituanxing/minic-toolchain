/* The section-to-segment inheritance also must not make BYTE writable
 * when the preceding output section uses an explicitly writable PHDR. */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x400000;
  .text : { *(.text) } :text
  . = ALIGN(0x1000);
  .data : { *(.data) } :data
  . = ALIGN(0x100);
  .edata_padding : { BYTE(0); . = ALIGN(0x200); }
}
