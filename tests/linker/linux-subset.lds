__alias = _start;
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = ((((-1))) - 0x80000000 + 1);
  _start = .;
  .head.text : AT(ADDR(.head.text) - ((((-1))) - 0x80000000 + 1)) { KEEP(*(.head.text)) }
  . = ALIGN((1 << 12));
  .text : { _text = .; *(.text.hot .text.hot.*) *(.text .text.fixup) _etext = .; }
  .init.data : AT(ADDR(.init.data) - ((((-1))) - 0x80000000 + 1)) { KEEP(*(SORT(___kentry+*))) *(.init.data .init.data.*) }
  .data : { *(.data) CONSTRUCTORS }
  .pecoff_edata_padding : { BYTE(0); . = ALIGN(0x200); }
  __raw_size = ABSOLUTE(. - _etext);
  .bss : { *(.bss) *(COMMON) }
  /DISCARD/ : { *(.note.GNU-stack) *(.discard.*) }
}
