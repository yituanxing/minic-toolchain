OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .byte_after_text : { BYTE(0x1234); }
  .short_after_text : { SHORT(0x123456); }
  .long_after_text : { LONG(0x123456789a); }
  .quad_after_text : { QUAD(0x8899aabbccddeeff); }
  . = ALIGN(0x1000);
  .data : { *(.data) }
  .byte_after_data : { BYTE(0x11); }
  .byte_preassign : { pre_assign = .; BYTE(0x12); }
  .byte_postassign : { BYTE(0x13); post_assign = .; }
  .byte_absassign : { abs_assign = ABSOLUTE(.); BYTE(0x14); }
  .byte_constassign : { const_assign = 123; BYTE(0x15); }
  .short_after_data : { SHORT(0x2233); }
  .long_after_data : { setup_sig = .; LONG(0x44556677); setup_size = ALIGN(ABSOLUTE(.),4096); setup_sects = ABSOLUTE(setup_size / 512); }
  .quad_after_data : { QUAD(0x8899aabbccddeeff); }
  . = ALIGN(16);
  .bss : { __bss_start = .; *(.bss); __bss_end = .; }
}
