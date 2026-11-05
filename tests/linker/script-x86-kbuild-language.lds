/* Kbuild's generated x86 linker script combines all these GNU constructs. */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}
SECTIONS {
  . = ALIGN(0x100123, 0x200000);
  .text : AT(ADDR(.text) - 0x200000) {
    _start = .;
    *(.text);
  } :text = 0xcc
  . = ALIGN(4096);
  .data : AT(ADDR(.data) - 0x200000) {
    data_begin = .;
    *(.data)
    . += 16;
    data_end = .;
  } :data
  .got.plt (INFO) : { *(.got.plt) }
  ASSERT(SIZEOF(.got.plt) == 0 || SIZEOF(.got.plt) == 0x18,
         "Unexpected GOT/PLT entries detected!")
}
PROVIDE(_alias = _start);
. = ASSERT((_start & 3) == 0, "entry must be 4-byte aligned");
