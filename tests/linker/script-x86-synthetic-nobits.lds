OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x400000;
  .text : { *(.text) } :text
  . = ALIGN(4096);
  /* Linux .orc_lookup is reserved by a script-only dot increment. */
  .orc_lookup : {
    orc_lookup = .;
    . += 0x2000;
    orc_lookup_end = .;
  } :data
}
