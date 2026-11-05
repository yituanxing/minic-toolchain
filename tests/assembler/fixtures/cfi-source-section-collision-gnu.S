.cfi_sections .eh_frame, .debug_frame
.section .eh_frame,"a",@progbits
.long 0
.section .debug_frame,"",@progbits
.long 0
.text
.globl cfi_mixed_section
cfi_mixed_section:
  .cfi_startproc
  .cfi_def_cfa 7, 8
  nop
  .cfi_endproc
