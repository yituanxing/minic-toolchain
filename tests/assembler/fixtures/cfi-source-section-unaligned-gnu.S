.cfi_sections .eh_frame, .debug_frame
.section .eh_frame,"a",@progbits
.byte 0x5a
.section .debug_frame,"",@progbits
.byte 0x5a
.text
.globl cfi_unaligned_source
cfi_unaligned_source:
  .cfi_startproc
  .cfi_def_cfa 7, 8
  nop
  .cfi_endproc
