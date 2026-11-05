.cfi_sections .eh_frame, .debug_frame
.text
.globl cfi_mixed_section
cfi_mixed_section:
  .cfi_startproc
  .cfi_def_cfa 7, 8
  nop
  .cfi_endproc
