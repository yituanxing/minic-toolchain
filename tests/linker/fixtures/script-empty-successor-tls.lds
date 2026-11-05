SECTIONS
{
  . = 0x10000;
  .text : { *(.text) }
  . = 0x20000;
  .empty : { before_tls = .; }
  top_before_tls = .;
  .tdata : { *(.tdata) }
  after_tls = .;
  . = 0x30000;
  .data : { *(.data) }
}
