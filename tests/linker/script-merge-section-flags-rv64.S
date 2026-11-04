.section .text,"ax",@progbits
.globl _start
_start:
    addi a0, zero, 0
    addi a7, zero, 93
    ecall

.section .rodata.str1.1,"aMS",@progbits,1
.globl merge_text
merge_text:
    .asciz "alpha\n"

.section .rodata.cst4,"aM",@progbits,4
.balign 4
.globl merge_const
merge_const:
    .long 0x12345678

.section .rodata,"a",@progbits
.globl normal_const
normal_const:
    .quad 0x0123456789abcdef
