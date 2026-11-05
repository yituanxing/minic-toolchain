.section .text,"ax",@progbits
.globl _start
_start:
    adrp x0, a_string
    add x0, x0, :lo12:a_string
    adrp x1, b_string
    add x1, x1, :lo12:b_string
    ret

.section .rodata.str1.8,"aMS",@progbits,1
.p2align 3
.globl a_string
a_string:
    .asciz "shared-AArch64-merge-string"

.section .data,"aw",@progbits
.globl relative_a
relative_a:
    .word a_string - .
.globl relative_b
relative_b:
    .word b_string - .
