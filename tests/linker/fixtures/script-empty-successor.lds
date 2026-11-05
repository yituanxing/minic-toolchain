SECTIONS
{
  . = 0x20000;
  .empty : { empty_start = .; absolute_start = ABSOLUTE(.); }
  top_before = .;
  .data 0 : AT(0x20000) { *(.data) }
  . = 0x30000;
  .text : { *(.text) }
  .tail : { tail_empty = .; }
}
