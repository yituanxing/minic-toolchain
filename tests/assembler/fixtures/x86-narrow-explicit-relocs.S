.text
.globl explicit_start
explicit_start:
    .byte 0
    .short 0
    .byte 0
    .short 0
    .reloc 0, R_X86_64_8, abs_byte+1
    .reloc 1, R_X86_64_16, abs_half+2
    .reloc 3, R_X86_64_PC8, cross_data
    .reloc 4, R_X86_64_PC16, cross_data
.data
.globl cross_data
cross_data:
    .long 0
